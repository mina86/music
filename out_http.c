/*
 * "Listening to" daemon MPD input module
 * $Id: out_http.c,v 1.7 2007/09/26 18:03:57 mina86 Exp $
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
static int   module_start(const struct music_module *restrict m)
	__attribute__((nonnull));


/**
 * Stops module.  See music_module::stop.
 *
 * @param m out_http module to stop.
 */
static void  module_stop (const struct music_module *restrict m)
	__attribute__((nonnull));


/**
 * Frees memory allocated by module.  See music_module::free.
 *
 * @param m out_http module to free.
 */
static void  module_free (struct music_module *restrict m)
	__attribute__((nonnull));


/**
 * Accepts configuration options.  See music_module::conf.
 *
 * @param m out_http module.
 * @param opt option keyword.
 * @param arg argument.
 * @return whether option was accepted.
 */
static int   module_conf (const struct music_module *restrict m,
                          const char *restrict opt,
                          const char *restrict arg)
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
static int   module_send (const struct music_module *restrict m,
                          const struct music_song *restrict const *restrict songs,
                          size_t *restrict errorPositions)
	__attribute__((nonnull(1, 2)));



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
static size_t escape(char *restrict dest, const char *restrict src, size_t n)
	__attribute__((nonnull));


/**
 * Calculates length of escaped string.  This method does not count
 * NUL byte.
 *
 * @param src  string to calculate lengthescape.
 * @return length of escaped string.
 */
static size_t escapeLength(const char *restrict src)
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
static size_t got_header(const char *restrict str, size_t size, size_t n,
                         void *restrict arg)
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
static size_t got_body  (const char *restrict str, size_t size, size_t n,
                         void *restrict arg)
	__attribute__((nonnull));


/**
 * Callback function for libcurl.  Called when library sends some
 * debug information.
 *
 * @param request CURL easy interface handler.
 * @param type message type.
 * @param data message (not zero terminated).
 * @param length message's length.
 * @param arg out_http module.
 * @return 0.
 */
static int    got_debug (CURL *restrict request, curl_infotype type,
                         const char *restrict data, size_t length,
                         void *restrict arg);




/**
 * Module's configuration.
 */
struct module_config {
	pthread_mutex_t mutex;  /**< Mutex used when aborting request. */
	CURL *request;          /**< CURL object for handling HTTP requests. */
	char *url;              /**< Request's URL. */
	char *username;         /**< User name. */
	char password[20];      /**< SHA1 of password. */
	short gotPassword;      /**< Whether password was given in
                                 configuration file. */
	short verbose;          /**< Whether CURL should be verbose. */
	time_t waitTill;        /**< Wait with submitting songs till that
	                             moment. */
	unsigned lastWait;      /**< How much time did we wait last time. */
};


/**
 * Module's User Agent string as sent when doing HTTP request.  This
 * is a "music-out_http/x.y libcurl/a.b.c" where x.y is module's
 * version and a.b.c is libcurl's version.  This is initalised once by
 * init().
 */
static char userAgent[64] = "";

/**
 * Headers sent while making request.
 */
struct curl_slist headers = { (char*)"Accept: text/x-music", 0 };



struct music_module *init(const char *restrict name,
                          const char *restrict arg) {
	struct module_config *cfg;
	struct music_module *const m = malloc(sizeof *m + sizeof *cfg);
	(void)name; /* supress warning */
	(void)arg;  /* supress warning */

	m->type        = MUSIC_OUT;
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
	cfg->gotPassword = cfg->verbose = 0;

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



static int   module_start(const struct music_module *restrict m) {
	struct module_config *const cfg = m->data;
	CURL *const request = cfg->request = curl_easy_init();

	if (!request) {
		return 0;
	}

	curl_easy_setopt(request, CURLOPT_USERAGENT     , userAgent);
	curl_easy_setopt(request, CURLOPT_WRITEFUNCTION , got_body);
	curl_easy_setopt(request, CURLOPT_HEADERFUNCTION, got_header);
	curl_easy_setopt(request, CURLOPT_DEBUGFUNCTION , got_debug);
	curl_easy_setopt(request, CURLOPT_DEBUGDATA     , (void*)m);
	curl_easy_setopt(request, CURLOPT_URL           , cfg->url);
	curl_easy_setopt(request, CURLOPT_VERBOSE       , (long)cfg->verbose);
	curl_easy_setopt(request, CURLOPT_STDERR        , (void*)stderr);
	curl_easy_setopt(request, CURLOPT_HTTPHEADER    , (void*)&headers);
	return 1;
}



static void  module_stop (const struct music_module *restrict m) {
	struct module_config *const cfg = m->data;
	pthread_mutex_lock(&cfg->mutex);
	if (cfg->request) {
		curl_easy_cleanup(cfg->request);
		cfg->request = 0;
	}
	pthread_mutex_unlock(&cfg->mutex);
}



static void  module_free (struct music_module *restrict m) {
	struct module_config *const cfg = m->data;
	pthread_mutex_destroy(&cfg->mutex);
	free(cfg->username);
	free(cfg->url);
}



static int   module_conf (const struct music_module *restrict m,
                          const char *restrict opt,
                          const char *restrict arg) {
	static const struct music_option options[] = {
		{ "url",      1, 1 },
		{ "username", 1, 2 },
		{ "password", 1, 3 },
		{ "verbose",  0, 4 },
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

	case 4:
		cfg->verbose = 1;
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
static size_t addSong(char *restrict data, size_t pos, size_t capacity,
                      const struct music_song *restrict song)
	__attribute__((nonnull));



/**
 * Request data or state.
 */
struct request_data {
	const struct music_module *m;  /**< out_http module. */

	const struct music_song *restrict const *songs;  /**< Pointer to song array. */
	size_t songPos;                /**< Position of current song. */
	size_t count;                  /**< Number of songs in request. */
	size_t handled;                /**< Number of songs handled by request. */

	size_t *errorPositions;        /**< Array to save error positions. */
	size_t errorPos;               /**< Number of songs that failed so far. */

	char *data;      /**< Temporary storage used when got_body()
	                      recieves part of respons which does not end
	                      with a full line. */
	size_t data_len; /**< Length of data. */

	enum {
		ST_IGNORE = -1,  /**< The rest of the response is to be ignored. */
		ST_START,        /**< We're starting reading the response --
		                      expecting "HTTP/1.x 200" header. */
		ST_HD_TYPE,      /**< Waiting for Content-Type header. */
		ST_HEADERS,      /**< We got HTTP status and reading headers. */
		ST_BODY_START,   /**< We're starting reading body -- epxecting
		                      "MUSIC <code>". */
		ST_BODY_CONT,    /**< We're continueing to read body. */
		ST_BODY_ERROR    /**< We're going to read error message. */
	} state;                       /**< State we are in. */

	int wait; /**< Don't do any more requests for this number of
	               seconds.  It can be also -1 which means don't do
	               any more requests at all. */
};



/**
 * Submits songs.  Number of songs thre are in the request must be saved in
 * count element of request_data structure pointed by d.  songPos should be
 * position of the *next* song to be processed, ie. the first one being
 * processed has positon songPos - count.
 *
 * When function returns songPos will be incremented by this value and for
 * each song function faild to submit its position (where first's song's
 * position is songPos-count) will be saved in errorPositions array at
 * postion errorPos and then errorPos will be incremented.
 *
 * Function returns non zero if we can continue submitting songs.  Otherwise
 * it will return zero.  If that happens caller must not submitt any other
 * songs.
 *
 * @param data POST data.
 * @param len length of POST data.
 * @param d request state.
 * @return whether further submissions can be issued.
 */
static int  sendSongs(const char *restrict data, size_t len,
                      struct request_data *restrict d)
	__attribute__((nonnull));



static int    module_send (const struct music_module *restrict m,
                           const struct music_song *restrict const *restrict songs,
                           size_t *restrict errorPositions) {
	struct module_config *const cfg = m->data;
	struct request_data d = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	size_t start = 0, pos, capacity;
	char *data;

	if (!*songs) return 0;

	pthread_mutex_lock(&cfg->mutex);
	if (!cfg->request ||
		(cfg->waitTill && cfg->waitTill>time(0)) ||
		!(data = malloc(capacity = 10224))) {
		pthread_mutex_unlock(&cfg->mutex);
		return -1;
	}
	cfg->waitTill = 0;

	d.m = m;
	d.songs = songs;
	d.errorPositions = errorPositions;

	/* Authentication */
	if (cfg->username) {
		time_t t = time(0);
		size_t l;
		start = sprintf(data, "auth=pass:%s:%lx:", cfg->username,
		                (unsigned long)t);
		memcpy((uint8_t*)data+start+30, cfg->password, 20);
		l = 20 + sprintf(data + start + 50, "%lx", (unsigned long)t);

		sha1_b64(data + start, (uint8_t*)data + start + 30, l);
		start += 27;
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
			if (!sendSongs(data, pos, &d)) break;

			pos = start;
			d.count = 0;
			add = addSong(data, pos, capacity, song);
		}

		if (add) {
			++d.count;
			pos += add;
		} else {
			music_log(m, LOG_WARNING, "song name too long (will not submit): "
			                          "%s <%s> %s",
			          song->artist ? song->artist : "(empty)",
			          song->album  ? song->album  : "(empty)",
			          song->title  ? song->title  : "(empty)");
		}
	}

	/* Send any pending songs */
	if (d.count) {
		sendSongs(data, pos, &d);
	}

	free(data);
	free(d.data);

	pthread_mutex_unlock(&cfg->mutex);

	/* Mark rest as invalid */
	if (errorPositions) {
		while (d.songPos < d.count) {
			errorPositions[d.errorPos++] = d.songPos++;
		}
	} else {
		d.errorPos += d.count - d.songPos;
	}

	return d.errorPos;
}



static size_t addSong(char *restrict data, size_t pos, size_t capacity,
                      const struct music_song *restrict song) {
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


static int  sendSongs(const char *restrict data, size_t len,
                      struct request_data *restrict d) {
	struct module_config *const cfg = d->m->data;
	CURL *const request = cfg->request;

	music_log(d->m, LOG_DEBUG - 2, "sendSongs()");

	d->songPos -= d->count;
	d->handled = 0;
	d->state = ST_START;
	d->wait = 0;
	curl_easy_setopt(request, CURLOPT_POSTFIELDS   , data);
	curl_easy_setopt(request, CURLOPT_POSTFIELDSIZE, (long)len);
	if (curl_easy_perform(request)) {
		d->wait = 60;
	}
	d->songPos += d->handled;

	if (d->errorPositions) {
		for (d->count -= d->handled; d->count; --d->count) {
			d->errorPositions[d->errorPos++] = d->songPos++;
		}
	} else {
		d->count    -= d->handled;
		d->songPos  += d->count;
		d->errorPos += d->count;
	}

	if (d->wait<0) {
		curl_easy_cleanup(request);
		cfg->request = 0;
		music_log(d->m, LOG_ERROR, "Won't submit songs any longer.");
		return 0;
	} else if (d->wait) {
		unsigned wait = cfg->lastWait;
		if (wait<(unsigned)d->wait) {
			wait = d->wait;
		} else if (wait < 15 * 60) {
			wait *= 2;
		}
		cfg->lastWait = wait;
		music_log(d->m, LOG_NOTICE, "Won't submit songs for %u seconds.",
		          wait);
		cfg->waitTill = time(0) + wait;
		return 0;
	} else {
		cfg->lastWait = 0;
	}
	return 1;
}



/****************************** Handle request ******************************/
/**
 * Extracts and handles single line from reply.
 *
 * This function starts parsing of data starting from position pointed
 * by pos.  Also, if whole line was parsed position of next line is
 * stored in location pointed by pos.  This way calling function can
 * check if there are any other lines to parse (ie. when location
 * pointed by pos stores other value then size) and if it is a whole
 * line (ie. when there are still characters but function returns 0).
 *
 * @param str  data string.
 * @param size number of characters in response.
 * @param d    request state.
 * @param pos  starting position of line and here is written starting
 *             position of next line.
 * @return whether line was sucessfully extracted.
 */
static int parse_reply_line(const char *restrict str, size_t size,
                            struct request_data *restrict d,
                            size_t *restrict pos)
	__attribute__((nonnull(1,3)));



/**
 * Handles single line from reply.  This function parses a line of reply
 * which begins at str and is length character long.
 *
 * @param str    line starting position.
 * @param length line's length.
 * @param d      request state.
 */
static void handle_reply_line(const char *restrict str, size_t length,
                              struct request_data *restrict d)
	__attribute__((nonnull));



static int parse_reply_line(const char *restrict str, size_t size,
                            struct request_data *restrict d,
                            size_t *restrict pos) {
	const char *start = str + (pos ? *pos : 0);
	const char *ch = start, *const end = str + size;
	size_t length;

	/* Skip white space */
	while (ch!=end && (*ch==' ' || *ch=='\t' || *ch=='\v' || *ch=='\f')) ++ch;
	start = ch;

	/* Find end of line */
	while (ch!=end && *ch!='\r' && *ch!='\n') ++ch;
	length = ch - start;

	/* "Premature" EOL */
	if (ch==end) {
		return 0;
	}

	/* Save position of next line */
	if (pos) {
		if (*ch++=='\r' && ch!=end && *ch=='\n') ++ch;
		*pos = ch - str;
	}

	/* Skip empty lines */
	if (!length) {
		return 1;
	}

	/* Handle */
	handle_reply_line(start, length, d);
	return 1;
}



static void handle_reply_line(const char *restrict str, size_t length,
                              struct request_data *restrict d) {
	char *const line = malloc(length + 1);
	unsigned num;
	int pos;

	memcpy(line, str, length);
	line[length] = 0;


	switch (d->state) {
	case ST_START:
		if (sscanf(line, "HTTP/%*u.%*u %u %n", &num, &pos)<1) {
			music_log(d->m, LOG_ERROR, "expected HTTP status not: %s", line);
			d->state = ST_IGNORE;
			d->wait  = 60;
		} else if (num!=200) {
			music_log(d->m, LOG_NOTICE, "HTTP status: %u %s", num, line+pos);
			d->state = ST_IGNORE;
			d->wait  = 60;
		} else {
			d->state = ST_HD_TYPE;
		}
		break;


	case ST_HD_TYPE: {
		char *ch;
		if (strncmp(line, "Content-Type:", 13)) {
			break;
		}

		for (ch = line + 13; isspace(*ch); ++ch);
		if (!strncmp(ch, "text/x-music", 12)
		    && (!ch[12] || ch[12]==';' || ch[12]==',' || isspace(ch[12]))) {
			d->state = ST_HEADERS;
		} else {
			music_log(d->m, LOG_NOTICE, "Invalid content type: %s", ch);
			d->state = ST_IGNORE;
			d->wait  = 60;
		}

		break;
	}


	case ST_BODY_START:
		if (sscanf(line, "MUSIC %u %n", &num, &pos)<1) {
			music_log(d->m, LOG_ERROR,
			          "expected server status not: %s", line);
			d->state = ST_IGNORE;
			d->wait  = 60;
		} else if (num/100!=1) {
			music_log(d->m, LOG_NOTICE, "Server status: %u %s", num,line+pos);
			d->state = ST_BODY_ERROR;
			d->wait = num/100==2 ? -1 : 60;
		} else {
			d->state = ST_BODY_CONT;
		}
		break;


	case ST_BODY_CONT: {
		const char *message;
		int logLevel;

		if (!strcmp(line, "END")) {
			d->state = ST_IGNORE;
			break;
		}

		if (sscanf(line, "SONG %u %n", &num, &pos)<1) {
			music_log(d->m, LOG_DEBUG, "ignoring line: %s", line);
			break;
		}

		if (num<d->handled || num>d->count) {
			break;
		}

		while (d->handled < num) {
			d->errorPositions[d->errorPos++] = d->songPos + d->handled++;
		}

		num += d->songPos;
		if (!strcmp(line+pos, "OK")) {
			message = "Song '%s <%s> %s' added.";
			logLevel = LOG_DEBUG;
		} else if (!strcmp(line+pos, "REJECTED")) {
			pos += 8;
			message = "Song '%s <%s> %s' rejected:%s";
			logLevel = LOG_WARNING;
		} else if (!strcmp(line+pos, "FAILED")) {
			d->errorPositions[d->errorPos++] = num;
			pos += 6;
			message = "Error when adding '%s <%s> %s':%s";
			logLevel = LOG_NOTICE;
		} else {
			d->errorPositions[d->errorPos++] = num;
			message = "Unknown status when adding '%s <%s> %s': %s";
			logLevel = LOG_NOTICE;
		}

		++d->handled;
		music_log(d->m, logLevel, message,
		          d->songs[num]->artist ? d->songs[num]->artist : "(empty)",
		          d->songs[num]->album  ? d->songs[num]->album  : "(empty)",
		          d->songs[num]->title  ? d->songs[num]->title  : "(empty)",
		          line + pos);
		break;
	}


	case ST_BODY_ERROR:
		music_log(d->m, LOG_NOTICE, "Server error message: %s", line);
		d->state = ST_IGNORE;
		break;


	case ST_IGNORE:
	case ST_HEADERS:
		/* We should never be here since this should be checked by
		   got_body and got_header functions. */
		break;
	}


	/* Free our buffer and return */
	free(line);
}



static size_t got_header(const char *restrict str, size_t size, size_t n,
                         void *restrict arg) {
	struct request_data *const d = arg;
	size *= n;

	if (d->state!=ST_IGNORE && d->state<ST_HD_TYPE) {
		parse_reply_line(str, size, d, 0);
	}
	return size;
}


static size_t got_body  (const char *restrict str, size_t size, size_t n,
                         void *restrict arg) {
	struct request_data *const d = arg;
	size_t pos = 0;
	size *= n;

	/* This should never happen really, but in general we should check. */
	if (d->state==ST_START) {
		music_log(d->m, LOG_ERROR, "Got body without headers?");
		d->state = ST_IGNORE;
		d->wait  = 60;
		return size;
	}
	if (d->state==ST_HD_TYPE) {
		music_log(d->m, LOG_ERROR, "Missing Content-Type header");
		d->state = ST_IGNORE;
		d->wait  = 60;
		return size;
	}
	if (d->state==ST_IGNORE) {
		return size;
	}
	if (d->state < ST_BODY_START) {
		d->state = ST_BODY_START;
	}

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

		parse_reply_line(d->data, d->data_len, d, 0);
		free(d->data);
		d->data_len = 0;
		pos = ch - str;
	}

	while (d->state!=ST_IGNORE && parse_reply_line(str, size, d, &pos));

	if (d->state!=ST_IGNORE && pos!=size) {
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



/****************************** Debugging ******************************/
static int    got_debug (CURL *restrict request, curl_infotype type,
                         const char *restrict data, size_t length,
                         void *restrict arg) {
	static const char *const types[CURLINFO_END] = {
		"",         /* CURLINFO_TEXT */
		"head < ",  /* CURLINFO_HEADER_IN */
		"head > ",  /* CURLINFO_HEADER_OUT */
		"data < ",  /* CURLINFO_DATA_IN */
		"data > ",  /* CURLINFO_DATA_OUT */
		"ssl < ",   /* CURLINFO_SSL_DATA_IN */
		"ssl > ",   /* CURLINFO_SSL_DATA_OUT */
	};

	const char *ch, *const end = data + length;
	size_t cap = 0, len;
	char *str = 0;

	(void)request;

	ch = data;
	do {
		for (data = ch; ch!=end && *ch!='\n' && *ch!='\r'; ++ch);
		len = ch - data;
		if (cap<len+1) {
			str = realloc(str, cap = len > 111 ? len + 1 : 112);
		}
		memcpy(str, data, len);
		str[len] = 0;

		music_log(arg, LOG_DEBUG, "curl: %s%s%s", types[type], str,
		          ch!=end ? "" : " --");
	} while (ch!=end && (*ch!='\r' || ++ch!=end) && (*ch!='\n' || ++ch!=end));

	free(str);
	return 0;
}
