/*
 * "Listening to" daemon MPD input module
 * $Id: out_http.c,v 1.11 2007/10/03 20:28:22 mina86 Exp $
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
                          const char *restrict opt, const char *restrict arg)
	__attribute__((nonnull(1)));


/**
 * Submits songs.  See music_module::song::submit.
 *
 * @param m out_http module.
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
 * Module's configuration.
 */
struct module_config {
	char *url;               /**< Request's URL. */
	char *username;          /**< User name. */
	char password[20];       /**< SHA1 of password. */
	time_t waitTill;         /**< Wait with submitting songs till that
	                              moment. */
	unsigned short lastWait; /**< How much time did we wait last time. */
	char gotPassword;        /**< Whether password was given in
                                   configuration file. */
	char verbose;            /**< Whether CURL should be verbose. */
};



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
	__attribute__((nonnull, pure));



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
	struct music_module *const m = music_init(MUSIC_OUT, sizeof *cfg);
	(void)name; /* supress warning */
	(void)arg;  /* supress warning */

	m->free          = module_free;
	m->config        = module_conf;
	m->song.send     = module_send;
	cfg              = m->data;
	cfg->username    = 0;
	cfg->url         = 0;
	cfg->gotPassword = 0;
	cfg->verbose     = 0;
	cfg->waitTill    = 0;
	cfg->lastWait    = 0;

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



static void  module_free (struct music_module *restrict m) {
	struct module_config *const cfg = m->data;
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
 * Structure holding data for given request.
 */
struct request {
	/** out_http module performing the request. */
	const struct music_module *m;
	/** CURL easy handler.  May be NULL if not yet initialised. */
	CURL *curl;

	/** Array of songs to submit. */
	const struct music_song *restrict const *songs;
	/** Number of handled songs. */
	size_t handled;

	/** Error informations. */
	struct request_error {
		/** Array where indexes of songs that failed are saved.  May
		 *  be NULL. */
		size_t *positions;
		/** Number of songs that failed. */
		size_t count;
	} error;

	/** POST data. */
	struct request_post {
		/** POST's data length. */
		size_t length;
		/** Position where accual songs starts (non zero means there
		 *  is auth[] argument. */
		size_t start;
		/** POST data.  Not zero terminated. */
		char data[10240];
	} post;

	/** State of particular request. */
	struct request_request {
		/** Number of songs in single request. */
		size_t count;
		/** Number of handled songs in single request. */
		size_t handled;
	} request;

	/** Buffer for holding non finished lines. */
	struct request_buffer {
		/**
		 * Buffer's data pointer.  We keep data in buffer NUL
		 * terminated so that there is no need to allocate new memory
		 * when passing bufer to request_handleLine() function.
		 */
		char *restrict data;
		size_t length;       /**< Data's length (expluding NUL terminator). */
		size_t capacity;     /**< Buffer's capacity. */
	} buffer;

	/** State of given request. */
	enum {
		ST_IGNORE,
		ST_HEADER_HTTP,
		ST_HEADER_TYPE,
		ST_HEADER_END,
		ST_BODY_STATUS,
		ST_BODY_CONT,
		ST_BODY_ERROR
	} state;


	enum {
		RT_OK = 0,

		RT_HTTP_INVALID,
		RT_HTTP_300,
		RT_HTTP_400,
		RT_HTTP_500,
		RT_HTTP_UNKNOWN,

		RT_TYPE_UNKNOWN,
		RT_TYPE_INVALID,

		RT_MUSIC_INVALID,
		RT_MUSIC_200,
		RT_MUSIC_300,
		RT_MUSIC_UNKNOWN,

		RT_CURL_ERROR
	} exitCode;
};



/**
 * Adds <tt>auth</tt> argument to POST data.  Also sets
 * <var>post.length</var> and <var>post.start</var> fields
 * aproprietly.
 *
 * @param r request data.
 * @param user user name.
 * @param pass password.
 */
void request_addAuth (struct request *restrict r,
                      const char *restrict user, const char *restrict pass)
	__attribute__((nonnull));



/**
 * Adds a song to the request.  If there is not enough space in POST
 * data buffer to add the song function will return 0.
 *
 * @param r request data.
 * @param song song to add.
 * @return whether song was succesfully added.
 */
int  request_addSong (struct request *restrict r,
                      const struct music_song *restrict song)
	__attribute__((nonnull));



/**
 * Initialises CURL easy handler.
 *
 * @param r request data.
 */
void request_curlInit(struct request *restrict r)  __attribute__((nonnull));



/**
 * Performs a single HTTP request.  This function alters
 * <var>handled</var> field of <var>request</var> structure.
 *
 * @param r request data.
 * @return whether module should continue performing requests.
 */
int  request_perform (struct request *restrict r)  __attribute__((nonnull));



/**
 * CURL callback called when library recieved HTTP headers.
 *
 * @param data data.
 * @param size size of single element.
 * @param n number of elements.
 * @param arg request data (cast to <tt>void*</tt>).
 */
size_t request_gotHead(const char *restrict data, size_t size, size_t n,
                       void *restrict arg)         __attribute__((nonnull));



/**
 * CURL callback called when library recieved HTTP body.
 *
 * @param data data.
 * @param size size of single element.
 * @param n number of elements.
 * @param arg request data (cast to <tt>void*</tt>).
 */
size_t request_gotBody(const char *restrict data, size_t size, size_t n,
                       void *restrict arg)         __attribute__((nonnull));



/**
 * Callback function for libcurl.  Called when library sends some
 * debug information.
 *
 * @param curl CURL easy interface handler.
 * @param type message type.
 * @param data message (not zero terminated).
 * @param length message's length.
 * @param arg out_http module.
 * @return 0.
 */
static int    got_debug (CURL *restrict curl, curl_infotype type,
                         const char *restrict data, size_t length,
                         void *restrict arg)       __attribute__((nonnull));



/**
 * Called by request_gotHead() and request_gotBody().  Extracts single
 * line from data and handles it (calls request_handleLine()).
 *
 * @param data data.
 * @param size data's length.
 * @param r request data.
 */
void   request_gotData(const char *restrict data, size_t size,
                       struct request *restrict r) __attribute__((nonnull));



/**
 * Handles single line.  If <var>nul</var> is true assumes that
 * <var>data</var> is null terminated and referenced data can be
 * modified.
 *
 * @param r request data.
 * @param data data.
 * @param len data's length.
 * @param nul whether data is nul terminated and modifiable.
 */
int    request_handleLine(struct request *restrict r,
                          const char *restrict data, size_t len, int nul)
	__attribute__((nonnull));



/**
 * Handles single line when request in in <var>ST_BODY_CONT</var>
 * state.
 *
 * @param r request data.
 * @param data nul terminated data.
 */
int    request_handleBodyCont(struct request *restrict r,
                              const char *restrict data)
	__attribute__((nonnull));



/**
 * Appends buffer in request with given data.  It is used by
 * request_gotData() when it needs to save partial line so it can
 * reconstruct whole line next time it's called.
 *
 * @param r request data.
 * @param data data.
 * @param size data's length.
 */
void   request_appendBuffer(struct request *restrict r,
                            const char *restrict data, size_t size)
	__attribute__((nonnull));



static int    module_send (const struct music_module *restrict m,
                           const struct music_song *restrict const *restrict songs,
                           size_t *restrict errorPositions) {
	const struct music_song *restrict const *s;
	struct module_config *const cfg = m->data;
	struct request *r;
	size_t handled;
	int ret;

	if (!*songs) {
		return 0;
	}

	if (cfg->waitTill && cfg->waitTill>time(0)) {
		return -1;
	}

	if (!(r = malloc(sizeof *r))) {
		return -1;
	}
	r->m = m;
	r->songs = s = songs;
	r->error.positions = errorPositions;
	r->handled = r->error.count = r->request.count = r->request.handled = 0;
	r->buffer.length = r->buffer.capacity = 0;
	r->buffer.data = 0;
	r->curl = 0;

	if (cfg->username) {
		request_addAuth(r, cfg->username, cfg->password);
	} else {
		r->post.length = r->post.start = 0;
	}

	do {
		if (request_addSong(r, *s)) {
			++s;
		} else if (!r->request.count) {
			music_log(r->m, LOG_WARNING,
			          "Song name too long '%s <%s> %s'",
			          (*s)->artist ? (*s)->artist : "(empty)",
			          (*s)->album  ? (*s)->album  : "(empty)",
			          (*s)->title  ? (*s)->title  : "(empty)");
			++s;
		} else if (!request_perform(r)) {
			break;
		}
	} while (*s);

	if (r->request.count) {
		request_perform(r);
	}

	if (r->curl) {
		curl_easy_cleanup(r->curl);
		r->curl = 0;
	}

	free(r->buffer.data);

	ret = r->error.count;
	handled = r->handled;
	free(r);

	if (*s) {
		if (errorPositions) {
			do errorPositions[ret++] = handled++; while (*++s);
		} else {
			while (*++s) ++ret;
		}
	}

	return ret;
}



void request_addAuth(struct request *restrict r,
                     const char *restrict user, const char *restrict pass) {
	char *const data = r->post.data;
	unsigned long t = time(0);
	size_t pos, len;

	pos = sprintf(data, "auth=pass:%s:%lx:", user, t);
	memcpy(data + pos + 30, pass, 20);
	len = 20 + sprintf(data + pos + 50, "%lx", t);
	sha1_b64(data + pos, (unsigned char *)data + pos + 30, len);
	r->post.start = r->post.length = pos + 27;
}



int  request_addSong(struct request *restrict r,
                     const struct music_song *restrict song) {
	size_t i = r->post.length, capacity = sizeof(r->post.data) - i;
	char *data = r->post.data + i;

	if (capacity < 13) {
		return 0;
	}

	/* Field name */
	i = i ? 8 : 7;
	memcpy(data, "&song[]=" + 8 - i, i);
	data += i; capacity -= i;

	/* String arguments */
	{
		const char *arr[4];
		arr[0] = song->title;
		arr[1] = song->artist;
		arr[2] = song->album;
		arr[3] = song->genre;

		i = 0;
		do {
			const size_t add = arr[i] ? escape(data, arr[i], capacity) : 0;
			if (add+1>=capacity) return 0;
			data[add] = ':';
			data += add + 1; capacity -= add - 1;
		} while (++i<4);
	}

	/* Numeric arguemnts */
	{
		int ret = snprintf(data, capacity, "%x:%lx", song->length,
		                   (unsigned long)song->endTime);
		if (ret<0 || (size_t)ret>capacity) {
			return 0;
		}
		data += (size_t)ret;
	}

	r->post.length = data - r->post.data;
	++r->request.count;
	return 1;
}



void request_curlInit(struct request *restrict r) {
	CURL *const curl = r->curl = curl_easy_init();
	struct module_config *const cfg = r->m->data;
	curl_easy_setopt(curl, CURLOPT_USERAGENT     , userAgent);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION , request_gotBody);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA     , (void*)r);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, request_gotHead);
	curl_easy_setopt(curl, CURLOPT_WRITEHEADER   , (void*)r);
	curl_easy_setopt(curl, CURLOPT_URL           , cfg->url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER    , (void*)&headers);
	if (cfg->verbose) {
		curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION , got_debug);
		curl_easy_setopt(curl, CURLOPT_DEBUGDATA     , (void*)r->m);
		curl_easy_setopt(curl, CURLOPT_VERBOSE       , 1L);
	}
}



int  request_perform(struct request *restrict r) {
	static const unsigned short waitTab[][2] = {
		{ 000,     0 }, /* RT_OK */

		{ 900,  1800 }, /* RT_HTTP_INVALID */
		{ 600,  3600 }, /* RT_HTTP_300 */
		{ 900,  3600 }, /* RT_HTTP_400 */
		{ 300,  1800 }, /* RT_HTTP_500 */
		{ 900,  1800 }, /* RT_HTTP_UNKNOWN */

		{ 600,  3600 }, /* RT_TYPE_UNKNOWN */
		{ 600,  3600 }, /* RT_TYPE_INVALID */

		{ 600,  1800 }, /* RT_MUSIC_INVALID */
		{ 300,  1800 }, /* RT_MUSIC_200 */
		{ 900,  3600 }, /* RT_MUSIC_300 */
		{ 600,  1800 }, /* RT_MUSIC_UNKNOWN */

		{ 900,  1800 }, /* RT_CURL_ERRO */
	};

	struct module_config *const cfg = r->m->data;
	unsigned wait;
	CURLcode code;

	/* Intialise CURL */
	if (!r->curl) {
		request_curlInit(r);
	}

	/* Zero state */
	r->state           = ST_HEADER_HTTP;
	r->exitCode        = RT_OK;
	r->request.handled = 0;
	r->buffer.length   = 0;

	/* Set POST data */
	curl_easy_setopt(r->curl, CURLOPT_POSTFIELDS   , r->post.data);
	curl_easy_setopt(r->curl, CURLOPT_POSTFIELDSIZE, (long)r->post.length);

	/* Perform */
	code = curl_easy_perform(r->curl);
	if (code!=CURLE_OK) {
		music_log(r->m, LOG_ERROR, "CURL: %s", curl_easy_strerror(code));
		r->exitCode = RT_CURL_ERROR;
	}

	/* Handle unhandled */
	if (r->request.count == r->request.handled) {
		/* do nothing */
	} else if (!r->error.positions) {
		r->error.count += r->request.count - r->request.handled;
	} else {
		size_t *const err = r->error.positions;
		size_t pos   = r->error.count;
		size_t count = r->request.count - r->request.handled;
		size_t hand  = r->handled;
		do {
			err[pos++] = hand++;
		} while (--count);
		r->error.count = pos;
	}

	/* Finalize */
	r->handled        += r->request.count;
	r->request.count   = 0;
	r->request.handled = 0;
	r->buffer.length   = 0;

	/* Analise exit code */
	if (r->exitCode==RT_OK) {
		cfg->lastWait = 0;
		cfg->waitTill = 0;
		return 1;
	}

	wait = waitTab[r->exitCode][0] <= cfg->lastWait
		? cfg->lastWait : waitTab[r->exitCode][0];
	wait <<= 1;
	wait = wait <= waitTab[r->exitCode][1]
		? wait : waitTab[r->exitCode][1];

	cfg->lastWait = wait;
	music_log(r->m, LOG_NOTICE, "Won't submit songs for next %u seconds.",
	          wait);
	cfg->waitTill = time(0) + wait;
	return 0;
}



size_t request_gotHead(const char *restrict data, size_t size, size_t n,
                       void *restrict arg) {
	struct request *const r = arg;

	size *= n;
	switch (r->state) {
	case ST_IGNORE:
	case ST_HEADER_END:
	case ST_BODY_STATUS:
	case ST_BODY_CONT:
	case ST_BODY_ERROR:
		break;

	case ST_HEADER_HTTP:
	case ST_HEADER_TYPE:
		request_gotData(data, size, r);
	}

	return size;
}



size_t request_gotBody(const char *restrict data, size_t size, size_t n,
                       void *restrict arg) {
	struct request *const r = arg;

	size *= n;
	switch (r->state) {
	case ST_IGNORE:
		break;

	case ST_HEADER_HTTP:
		music_log(r->m, LOG_ERROR, "No HTTP response before response body.");
		r->state    = ST_IGNORE;
		r->exitCode = RT_HTTP_INVALID;
		break;

	case ST_HEADER_TYPE:
		music_log(r->m, LOG_ERROR, "No Content-Type header.");
		r->state    = ST_IGNORE;
		r->exitCode = RT_TYPE_UNKNOWN;
		break;

	case ST_HEADER_END:
		r->state = ST_BODY_STATUS;
		/* FALL THROUGH */
	case ST_BODY_STATUS:
	case ST_BODY_CONT:
	case ST_BODY_ERROR:
		request_gotData(data, size, r);
		break;
	}

	return size;
}



void   request_gotData(const char *restrict data, size_t size,
                       struct request *restrict r) {
	const char *ch = data, *const end = data + size;

	do {
		const char *const line = ch;

		while (ch!=end && *ch!='\r' && *ch!='\n') ++ch;
		if (ch==end) {
			request_appendBuffer(r, data, size);
			break;
		}

		if (r->buffer.length) {
			int ret;
			request_appendBuffer(r, data, ch - data);
			ret = request_handleLine(r, r->buffer.data, r->buffer.length, 1);
			r->buffer.length = 0;
			if (!ret) break;
		} else if (!request_handleLine(r, line, ch - line, 0)) {
			break;
		}

		if (*ch++ == '\r' && ch != end && *ch == '\n') {
			++ch;
		}

	} while (ch!=end);
}



void   request_appendBuffer(struct request *restrict r,
                            const char *restrict data, size_t size) {
	size_t len  = r->buffer.length + size + 1;
	size_t need = ((len + 16 + 127) & ~(size_t)127) - 16;

	if (need < r->buffer.capacity) {
		r->buffer.data = realloc(r->buffer.data, need);
		r->buffer.capacity = need;
	}

	memcpy(r->buffer.data + r->buffer.length, data, size);
	r->buffer.data[r->buffer.length += size] = 0;
}



/**
 * Check if first string starts with the second one ignoring the case.
 * The second string must be <b>all lower case</b> or else the
 * comparison may give false negative answers.
 *
 * @param str string to check prefix.
 * @param pre the prefix.
 * @return whether strings starts with prefix ignoring case.
 */
static int string_starts(const char *restrict str, const char *restrict pre)
	__attribute__((nonnull, pure));

static int string_starts(const char *restrict str, const char *restrict pre){
	while (*pre) {
		char s = *str++, p = *pre++;
		if (isupper(s)) {
			s = tolower(s);
		}
		if (s!=p) {
			return 0;
		}
	}
	return 1;
}



int    request_handleLine(struct request *restrict r,
                          const char *restrict data, size_t len, int nul) {
	char *malloced = 0;
	int ret = 1, pos;
	unsigned num;

	while (len && isspace(*data)) {
		++data;
		--len;
	}

	while (len && isspace(data[len-1])) {
		--len;
	}

	if (!len) {
		return 1;
	}

	if (nul) {
		((char *)data)[len] = 0;
	} else {
		malloced = malloc(len + 1);
		memcpy(malloced, data, len);
		malloced[len] = 0;
		data = malloced;
	}



	switch (r->state) {
		/* Ignore; we should never get here honestly */
	case ST_IGNORE:
		break;


		/* Waiting for HTTP status line (HTTP/x.y ### <...>) */
	case ST_HEADER_HTTP:
		if (sscanf(data, " HTTP/%*u.%*u %u %n", &num, &pos)<1) {
			music_log(r->m, LOG_ERROR, "Invalid HTTP status line: %s", data);
			r->exitCode = RT_HTTP_INVALID;
			r->state    = ST_IGNORE;
			ret         = 0;
			break;
		}

		switch (num / 100) {
		case  2:
			r->state = ST_HEADER_TYPE;
			goto finish; /* escape 2 switches */

		case  3: r->exitCode = RT_HTTP_300    ; break;
		case  4: r->exitCode = RT_HTTP_400    ; break;
		case  5: r->exitCode = RT_HTTP_500    ; break;
		default: r->exitCode = RT_HTTP_UNKNOWN; break;
		}

		music_log(r->m, LOG_ERROR, "HTTP status: %u %s",  num, data + pos);
		r->state = ST_IGNORE;
		ret = 0;
		break;


		/* Waiting for Content-Type header */
	case ST_HEADER_TYPE:
		if (!string_starts(data, "content-type:")) {
			break;
		}

		for (data += 13; isspace(*data); ++data);
		if (!string_starts(data, "text/x-music")) {
			music_log(r->m, LOG_ERROR, "Invalid content-type: %s", data);
			r->exitCode = RT_TYPE_INVALID;
			r->state    = ST_IGNORE;
			ret         = 0;
			break;
		}

		r->state = ST_HEADER_END;
		break;


		/* Ignore the rest of headers; we should never get here honestly */
	case ST_HEADER_END:
		break;


		/* Wait for music status (MUSIC ### <...>) */
	case ST_BODY_STATUS:
		if (sscanf(data, "MUSIC %u %n", &num, &pos)<1) {
			music_log(r->m, LOG_ERROR, "Invalid Music status line: %s", data);
			r->exitCode = RT_MUSIC_INVALID;
			r->state    = ST_IGNORE;
			ret         = 0;
			break;
		}

		switch (num/100) {
		case  1:
			r->state = ST_BODY_CONT;
			goto finish; /* escape 2 switches */

		case  2: r->exitCode = RT_MUSIC_200    ; break;
		case  3: r->exitCode = RT_MUSIC_300    ; break;
		default: r->exitCode = RT_MUSIC_UNKNOWN; break;
		}


		music_log(r->m, LOG_ERROR, "Music status: %u %s",  num, data + pos);
		r->state = ST_BODY_ERROR;
		ret = 0;
		break;


		/* Parse body */
	case ST_BODY_CONT:
		ret = request_handleBodyCont(r, data);
		break;


		/* Read error message */
	case ST_BODY_ERROR:
		music_log(r->m, LOG_NOTICE, "Server error message: %s", data);
		r->state = ST_IGNORE;
		ret = 0;
		break;
	}


 finish:
	free(malloced);
	return ret;
}




int    request_handleBodyCont(struct request *restrict r,
                              const char *restrict data) {
	const struct music_song *restrict const *s;
	size_t base, handled;
	unsigned num;
	int pos;


	if (!strcmp(data, "END")) {
		r->state = ST_IGNORE;
		return 1;
	}

	if (sscanf(data, "SONG %u %n", &num, &pos)<1) {
		music_log(r->m, LOG_DEBUG, "ignoring line: %s", data);
		return 1;
	}
	data += pos;

	handled = r->request.handled;
	if (num < handled || num > r->request.count) {
		music_log(r->m, LOG_DEBUG, "ignoring line: %s", data);
		return 1;
	}


	base = r->handled;
	s = r->songs + base;
	do {
		const char *msg;
		int log, err;

		if (handled < num) {
			data += 0;
			msg   = "Missing status line for '%s <%s> %s'";
			log   = LOG_WARNING;
			err   = 1;
		} else if (!strcmp(data, "OK")) {
			data += 0;
			msg   = "Song '%s <%s> %s' added.";
			log   = LOG_DEBUG;
			err   = 0;
		} else if (!strcmp(data, "REJ")) {
			data += 3;
			msg   = "Song '%s <%s> %s' rejected:%s";
			log   = LOG_WARNING;
			err   = 0;
		} else if (!strcmp(data, "FAIL")) {
			data += 4;
			msg   = "Error when adding '%s <%s> %s':%s";
			log   = LOG_NOTICE;
			err   = 1;
		} else {
			data += 0;
			msg   = "Unknown status when adding '%s <%s> %s': %s";
			log   = LOG_NOTICE;
			err   = 1;
		}

		music_log(r->m, log, msg,
		          (*s)->artist ? (*s)->artist : "(empty)",
		          (*s)->album  ? (*s)->album  : "(empty)",
		          (*s)->title  ? (*s)->title  : "(empty)", data);

		if (!err) {
			/* do nothing */
		} else if (r->error.positions) {
			r->error.positions[r->error.count++] = base + num;
		} else {
			++r->error.count;
		}

		++s;
	} while (++handled <= num);


	r->request.handled = handled;
	return 1;
}



/**
 * Tells whether character needs to be escaped.
 *
 * @note As with ROL macro defined in sha1.c, escape_char inline
 * function was replaced by IS_ESCAPE_CHAR macro because my tests on
 * sha1 revealed that GCC has (had?) some problems with optimising
 * very simple inline functions and therefore now we have macro here
 * instead of inline function.
 *
 * @param ch character to chec.
 * @return 1 if charactr needs to be escaped, 0 otherwise.
 */
#if 1
#  define IS_ESCAPE_CHAR(ch) \
	(((unsigned char)(ch)) < 0x30 || (((unsigned char)(ch)) > 0x39 && \
	 ((unsigned char)(ch)) < 0x41) || ((unsigned char)(ch)) > 0x7f);
#else
static inline int IS_ESCAPE_CHAR(unsigned char ch)
	__attribute__((always_inline));
static inline int IS_ESCAPE_CHAR(unsigned char ch) {
	return ch < 0x30 || (ch > 0x39 && ch < 0x41) || ch > 0x7f;
}
#endif


static size_t escape(char *dest, const char *src, size_t n) {
	static const char xdigits[16] = "0123456789ABCDEF";

	size_t pos = 0;
	for (; *src; ++src) {
		const unsigned char ch = (unsigned char)*src;
		if (IS_ESCAPE_CHAR(ch)) {
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
		count += (IS_ESCAPE_CHAR(*src) << 1) | 1;
	}
	return count;
}


#if defined IS_ESCAPE_CHAR
#  undef IS_ESCAPE_CHAR
#endif




static int    got_debug (CURL *restrict curl, curl_infotype type,
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

	(void)curl;

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
