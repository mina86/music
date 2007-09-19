/*
 * "Listening to" daemon MPD input module
 * $Id: out_http.c,v 1.4 2007/09/19 14:06:10 mina86 Exp $
 * Copyright (c) 2007 by Michal Nazarewicz (mina86/AT/mina86.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "music.h"
#include "sha1.h"

#include <ctype.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

#include <curl/curl.h>


/**
 * Starts module.  See music_module::start.
 *
 * @param m out_http module to start.
 * @return whether starting succeed.
 */
static int   module_start(const struct music_module *m)
	__attribute__((nonnull));


/**
 * Stops module.  See music_module::stop.
 *
 * @param m out_http module to stop.
 */
static void  module_stop (const struct music_module *m)
	__attribute__((nonnull));


/**
 * Frees memory allocated by module.  See music_module::free.
 *
 * @param m out_http module to free.
 */
static void  module_free (struct music_module *m) __attribute__((nonnull));


/**
 * Accepts configuration options.  See music_module::conf.
 *
 * @param m out_http module.
 * @param opt option keyword.
 * @param arg argument.
 * @return whether option was accepted.
 */
static int   module_conf (const struct music_module *m, const char *opt,
                          const char *arg)
	__attribute__((nonnull(1)));


/**
 * Submits songs.  See music_module::song::submit.
 *
 * @param out_http module.
 * @param songs NULL terminated list of songs to submit.
 * @param errorPositions output array with positions of errors that
 *                       failed to be submitted.
 * @return number of songs that function waild to submit or -1 which
 *          means all songs failed to be submitted.
 */
static int   module_send (const struct music_module *m,
                          const struct music_song *const *songs,
                          size_t *errorPositions)
	__attribute__((nonnull(1)));



/**
 * Escapes given string.  It replaces characters which may cause
 * problems into a per cent sign followed by two hexadecimal digits
 * which represents byte's value.  This method <strong>dose
 * not</strong> terminate escaped string with a NUL byte.
 *
 * It will write at moset n bytes and return length of escaped string
 * so if result is greater then n not whole encoded string fitted into
 * destination and user may not relay on the fact that first n chars
 * are valid
 *
 * @param dest destination where to save escaped string.
 * @param src  string to escape.
 * @param n    size of dest array.
 * @return length of escaped string.
 */
static size_t escape(char *dest, const char *src, size_t n)
	__attribute__((nonnull));


/**
 * Calculates length of escaped string.  This method does not count
 * NUL byte.
 *
 * @param src  string to calculate lengthescape.
 * @return length of escaped string.
 */
static size_t escapeLength(const char *src)
	__attribute__((nonnull));



/**
 * Callback function for libcurl.  Called when library retieved a HTTP
 * header.
 *
 * @param str array.
 * @param size size of single element.
 * @param n number of elements.
 * @param arg callback argument.
 * @return number of bytes function took care of (size*n on success).
 */
static size_t got_header(const char *str, size_t size, size_t n, void *arg)
	__attribute__((nonnull));


/**
 * Callback function for libcurl.  Called when library retieved
 * a respons body.
 *
 * @param str array.
 * @param size size of single element.
 * @param n number of elements.
 * @param arg callback argument.
 * @return number of bytes function took care of (size*n on success).
 */
static size_t got_body  (const char *str, size_t size, size_t n, void *arg)
	__attribute__((nonnull));




/**
 * Module's configuration.
 */
struct module_config {
	pthread_mutex_t mutex;  /**< Mutex used when aborting request. */
	CURL *request;          /**< CURL object for handling HTTP requests. */
	char *url;              /**< Request's URL. */
	char *username;         /**< User name. */
	char password[20];      /**< SHA1 of password. */
	int gotPassword;        /**> Whether password was given in
                                 configuration file. */
};


/**
 * Module's User Agent string as sent when doing HTTP request.  This
 * is a "music-out_http/x.y libcurl/a.b.c" where x.y is module's
 * version and a.b.c is libcurl's version.  This is initalised once by
 * init().
 */
static char userAgent[64] = "";



struct music_module *init(const char *name, const char *arg) {
	struct module_config *cfg;
	struct music_module *const m = malloc(sizeof *m + sizeof *cfg);
	(void)name; /* supress warning */
	(void)arg;  /* supress warning */

	m->type        = MUSIC_IN;
	m->start       = module_start;
	m->stop        = module_stop;
	m->free        = module_free;
	m->config      = module_conf;
	m->song.send   = module_send;
	m->retryCached = 0;
	cfg = m->data  = m + 1;

	pthread_mutex_init(&cfg->mutex, 0);
	cfg->request = 0;
	cfg->username = cfg->url = 0;
	cfg->gotPassword = 0;

	if (music_run_once_check((void(*)(void))curl_global_init, 0)) {
		curl_global_init(CURL_GLOBAL_ALL);
		atexit(curl_global_cleanup);
	}

	if (!*userAgent) {
		unsigned ver = curl_version_info(CURLVERSION_NOW)->version_num;
		sprintf(userAgent, "music-out_http/1.0 libcurl/%u.%u.%u",
		        (ver>>16) & 0xff, (ver>>8) & 0xff, ver & 0xff);
	}


	return m;
}



static int   module_start(const struct music_module *m) {
	struct module_config *const cfg = m->data;
	CURL *const request = cfg->request = curl_easy_init();

	if (!request) {
		return 0;
	}

	curl_easy_setopt(request, CURLOPT_USERAGENT     , userAgent);
	curl_easy_setopt(request, CURLOPT_WRITEFUNCTION , got_body);
	curl_easy_setopt(request, CURLOPT_HEADERFUNCTION, got_header);
	curl_easy_setopt(request, CURLOPT_URL           , cfg->url);
	return 1;
}



static void  module_stop (const struct music_module *m) {
	struct module_config *const cfg = m->data;
	pthread_mutex_lock(&cfg->mutex);
	if (cfg->request) {
		curl_easy_cleanup(cfg->request);
		cfg->request = 0;
	}
	pthread_mutex_unlock(&cfg->mutex);
}



static void  module_free (struct music_module *m) {
	struct module_config *const cfg = m->data;
	pthread_mutex_destroy(&cfg->mutex);
	free(cfg->username);
	free(cfg->url);
}



static int   module_conf (const struct music_module *m,
                          const char *opt, const char *arg) {
	static const struct music_option options[] = {
		{ "url",      1, 1 },
		{ "username", 1, 2 },
		{ "password", 1, 3 },
		{ 0, 0, 0 }
	};
	struct module_config *const cfg = m->data;

	/* Check configuration */
	if (!opt) {
		int ret = 1;
		if (!cfg->url) {
			music_log(m, LOG_FATAL, "url not set");
			ret = 0;
		}
		if (cfg->gotPassword) {
			if (!cfg->username) {
				music_log(m, LOG_FATAL, "password set but username not");
				ret = 0;
			}
		} else if (cfg->username) {
			music_log(m, LOG_FATAL, "username set but password not");
			ret = 0;
		}
		return ret;
	}

	/* Accept options */
	switch (music_config(m, options, opt, arg, 1)) {
	case 1:
		cfg->url = music_strdup_realloc(cfg->url, arg);
		break;

	case 2:
		if (strlen(arg)>128) {
			music_log(m, LOG_FATAL, "username too long");
			return 0;
		}
		{
			size_t l = escapeLength(arg);
			cfg->username = realloc(cfg->username, l);
			escape(cfg->username, arg, l);
		}
		break;

	case 3:
		sha1((uint8_t*)cfg->password, (uint8_t*)arg, strlen(arg));
		break;

	default:
		return 0;
	}

	return 1;
}



/**
 * Adds song to POST data.  Returns number of characters it consumed
 * or zero if there were not enough room.  If pos is non-zero also an
 * ampersand will be added at the beginning.
 *
 * @param data POST data string.
 * @param pos position to put song POST data.
 * @param capacity capacity of data array.
 * @param song song to add.
 * @return number of characters string consumed or zero if there was
 *         not enough room.
 */
static size_t addSong(char *data, size_t pos, size_t capacity,
                      const struct music_song *song) __attribute__((nonnull));



/**
 * Request data or state.
 */
struct request_data {
	const struct music_module *m;  /**< out_http module. */
	size_t *errorPositions;        /**< Array to save error positions. */
	size_t songPos;                /**< Position of current song. */
	size_t count;                  /**< Number of songs in request. */
	size_t handled;                /**< Number of songs handled by request. */
	size_t errorPos;               /**< Number of songs that failed so far. */
	char *data;      /**< Temporary storage used when got_body()
	                      recieves part of respons which does not end
	                      with a full line. */
	size_t data_len; /**< Length of data. */
};



/**
 * Submits songs.
 *
 * @param data POST data.
 * @param len length of POST data.
 * @param d request state.
 */
static void sendSongs(const char *data, size_t len, struct request_data *d)
	__attribute__((nonnull));



static int    module_send (const struct music_module *m,
                           const struct music_song *const *songs,
                           size_t *errorPositions) {
	struct module_config *const cfg = m->data;
	struct request_data d = {
		0, 0,
		0, 0, 0, 0,
		0, 0
	};
	size_t start = 0, pos, capacity;
	char *data;

	d.m = m;
	d.errorPositions = errorPositions;

	if (!*songs) return 0;

	pthread_mutex_lock(&cfg->mutex);
	if (!cfg->request) {
		goto finishNoFree;
	}

	if (!(data = malloc(capacity = 10224))) {
		goto finish;
	}


	/* Authentication */
	if (cfg->username) {
		time_t t = time(0);
		start = sprintf(data, "auth=pass:%s:%lx:", cfg->username,
		                (unsigned long)t);
		memcpy((uint8_t*)data+start+30, cfg->password, 20);
		sha1_b64(data + start, (uint8_t*)data + start + 30,
		         20 + sprintf(data + start + 50, "%lx", (unsigned long)t));
		start += 28;
	}


	/* Do */
	curl_easy_setopt(cfg->request, CURLOPT_WRITEDATA    , (void*)&d);
	curl_easy_setopt(cfg->request, CURLOPT_WRITEHEADER  , (void*)&d);

	pos = start;
	for (; songs[d.songPos]; ++d.songPos) {
		const struct music_song *const song = songs[d.songPos];
		size_t add = d.count < 32 ? addSong(data, pos, capacity, song) : 0;

		if (!add && d.count) {
			d.data_len = 0;
			sendSongs(data, pos, &d);

			pos = start;
			d.count = 0;
			add = addSong(data, pos, capacity, song);
		}

		if (add) {
			++d.count;
			pos += add;
		} else {
			music_log(m, LOG_ERROR, "song name too long (will not submit): "
			                        "%s - %s - %s",
			          song->artist ? song->artist : "Unknown artist",
			          song->album  ? song->album  : "Unknown album",
			          song->title  ? song->title  : "Unknown title");
		}
	}

	/* Send any pending songs */
	if (d.count) {
		sendSongs(data, pos, &d);
	}


 finish:
	free(data);
	free(d.data);
 finishNoFree:
	pthread_mutex_unlock(&cfg->mutex);

	/* Mark rest as invalid */
	while (d.songPos < d.count) {
		errorPositions[d.errorPos++] = d.songPos++;
	}
	return d.errorPos;
}



static size_t addSong(char *data, size_t pos, size_t capacity,
                      const struct music_song *song) {
	const size_t orgPos = pos;

	if (capacity - pos < 13) {
		return 0;
	}

	/* Field name */
	{
		unsigned addAmp = !!pos;
		memcpy(data + pos, "&song[]=" + !addAmp, 7 + addAmp);
		pos  += 7 + addAmp;
	}

	/* String arguments */
	{
		size_t i = 0;
		const char *arr[4];
		arr[0] = song->title;
		arr[1] = song->artist;
		arr[2] = song->album;
		arr[3] = song->genre;

		do {
			size_t add = 0;
			if (arr[i]) add = escape(data+pos, arr[i], capacity-pos);
			pos += add;
			if (pos+1>=capacity) return 0;
			data[pos++] = ':';
		} while (++i<4);
	}

	/* Numeric arguemnts */
	{
		int ret = snprintf(data+pos, capacity-pos, "%lx:%lx",
		                   (unsigned long)song->length,
		                   (unsigned long)song->endTime);
		if (ret<0 || (unsigned)ret>=capacity-pos) {
			return 0;
		}
		pos += (size_t)ret;
	}

	return pos - orgPos;
}


static void sendSongs(const char *data, size_t len, struct request_data *d) {
	CURL *const request = ((struct module_config*)d->m->data)->request;
	d->songPos -= d->count;
	d->handled = 0;
	curl_easy_setopt(request, CURLOPT_POSTFIELDS   , data);
	curl_easy_setopt(request, CURLOPT_POSTFIELDSIZE, (long)len);
	curl_easy_perform(request);
	d->songPos += d->handled;
	for (d->count -= d->handled; d->count; --d->count) {
		d->errorPositions[d->errorPos++] = d->songPos++;
	}
}



/****************************** Handle request ******************************/
/**
 * Handles single line from reply.
 *
 * This function starts parsing of data starting from position pointed
 * by pos.  Also, if whole line was parsed position of next line is
 * stored in location pointed by pos.  This way calling function can
 * check if there are any other lines to parse (ie. when location
 * pointed by pos stores other value then size) and if it is a whole
 * line (ie. when there are still characters but function returns 0).
 *
 * @param body whether it is body of respons (or still headers).
 * @param str  data string.
 * @param size number of characters in response.
 * @param d    request state.
 * @param pos  starting position of line and here is written starting
 *             position of next line.
 * @return whether line was sucessfully parsed.
 */
static int handle_reply_line(int body, const char *str, size_t size,
                             struct request_data *d, size_t *pos) {
	const char *ch = str, *end = str + size;
	size_t length;

	(void)body;
	(void)d;

	if (pos) {
		ch += *pos;
	}

	while (ch!=end && *ch!='\r' && *ch!='\n') ++ch;
	length = str - ch;

	if (ch==end) {
		return 0;
	}

	if (pos) {
		if (*ch++=='\r' && ch!=end && *ch=='\n') ++ch;
		*pos = ch - str;
	}

	end = str + length;
	fwrite("line: ", 1, 6, stdout);
	fwrite(str, 1, length, stdout);
	fwrite("\n", 1, 1, stdout);
	return 1;
}


static size_t got_header(const char *str, size_t size, size_t n, void *arg) {
	handle_reply_line(0, str, size * n, arg, 0);
	return size;
}


static size_t got_body  (const char *str, size_t size, size_t n, void *arg) {
	struct request_data *d = arg;
	size_t pos = 0;

	size *= n;

	if (d->data_len) {
		const char *ch = str, *const end = str + size;
		int found;

		while (ch!=end && *ch!='\r' && *ch!='\n') ++ch;
		if ((found = ch!=end)) {
			++ch;
		}

		d->data = realloc(d->data, d->data_len + size);
		memcpy(d->data + d->data_len, str, size);
		d->data_len += size;

		if (!found) {
			return size;
		}

		handle_reply_line(1, d->data, d->data_len, d, 0);
		free(d->data);
		d->data_len = 0;
		pos = ch - str;
	}

	while (handle_reply_line(1, str, size, d, &pos));

	if (pos!=size) {
		d->data = realloc(d->data, d->data_len = size - pos);
		memcpy(d->data, str+pos, size-pos);
	}

	return size;
}




/****************************** Escape ******************************/
/**
 * Tells whether character needs to be escaped.
 *
 * @param ch character to chec.
 * @return 1 if charactr needs to be escaped, 0 otherwise.
 */
static __inline__ int escape_char(unsigned char ch) {
	return ch < 0x30 || (ch > 0x39 && ch < 0x41) || ch > 0x7f;
}


static size_t escape(char *dest, const char *src, size_t n) {
	static const char xdigits[16] = "0123456789ABCDEF";

	size_t pos = 0;
	for (; *src; ++src) {
		const unsigned char ch = (unsigned char)*src;
		if (escape_char(ch)) {
			if (pos+2<n) {
				dest[pos++] = '%';
				dest[pos++] = xdigits[ch >> 4];
				dest[pos++] = xdigits[ch & 15];
			} else {
				pos += 3;
			}
		} else {
			if (pos<n) {
				dest[pos++] = ch;
			} else {
				++pos;
			}
		}
	}
	return pos;
}

static size_t escapeLength(const char *src) {
	size_t count = 0;
	for (; *src; ++src) {
		count += (escape_char(*src) << 1) | 1;
	}
	return count;
}
