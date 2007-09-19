/*
 * "Listening to" daemon library functions implementation
 * $Id: music-impl.c,v 1.2 2007/09/19 13:09:03 mina86 Exp $
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

#include "music-int.h"

#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_POLL
# include <poll.h>
#else
# include <sys/select.h>
# include <sys/time.h>
# include <sys/types.h>
# include <unistd.h>
#endif



/**
 * Internal logging functions.  This is music_log() and
 * music_log_errno() in one function and variants are destynguished
 * from the last argument.
 *
 * @param m module that raports message.
 * @param level message's level.
 * @param fmt message's format.
 * @param ap format arguments.
 * @param errStr whether to append error string.
 */
static void music_log_internal(const struct music_module *m, unsigned level,
                               const char *fmt, va_list ap, int errStr);



void music_log (const struct music_module *m, unsigned level,
                const char *fmt, ...){
	va_list ap;
	va_start(ap, fmt);
	music_log_internal(m, level, fmt, ap, 0);
}



void music_log_errno(const struct music_module *m, unsigned level,
                     const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	music_log_internal(m, level, fmt, ap, 1);
}



/**
 * Internal logging functions.  Performs the accual logging to given stream.
 *
 * @param stream stream to log message to.
 * @param date date string.
 * @param name module's name.
 * @param fmt message's format.
 * @param ap format arguments.
 * @param error error string or NULL.
 */
static void music_log_internal_do(FILE *stream, const char *date,
                                  const char *name,
                                  const char *fmt, va_list ap,
                                  const char *error)
	__attribute__((nonnull(1,2,3)));



static void music_log_internal(const struct music_module *m, unsigned level,
                               const char *fmt, va_list ap, int errStr) {
	static char buf[32];
	struct config *cfg = m->core->data;
	char *str;
	time_t t;

	if (cfg->loglevel < level) return;

	t = time(0);
	if (!strftime(buf, sizeof buf, "[%Y/%m/%d %H:%M:%S] ", gmtime(&t))) {
		buf[0] = 0;
	}

	pthread_mutex_lock(&cfg->log_mutex);

	str = errStr ? strerror(errno) : 0;

	if (cfg->logboth) {
		va_list ap2;
#if defined __GNUC__ && defined __va_copy
		/* In case -pedantic -ansi was added */
		__va_copy(ap2, ap);
#else
		va_copy(ap2, ap);
#endif
		music_log_internal_do(stderr, buf, m->name, fmt, ap, str);
		music_log_internal_do(stdout, buf, m->name, fmt, ap2, str);
	} else {
		music_log_internal_do(stderr, buf, m->name, fmt, ap, str);
	}

	pthread_mutex_unlock(&cfg->log_mutex);
}



static void music_log_internal_do(FILE *stream, const char *date,
                                  const char *name,
                                  const char *fmt, va_list ap,
                                  const char *error) {
	fprintf(stream, "%s%s: ", date, name);
	vfprintf(stream, fmt, ap);
	va_end(ap);
	if (error) {
		fprintf(stream, ": %s\n", error);
	} else {
		putc('\n', stream);
	}
}



int   music_config(const struct music_module *m, const struct music_option *options,
                   const char *opt, const char *arg, int req) {
	const struct music_option *o = options;
	while (o->opt && strcmp(o->opt, opt)) ++o;
	if (!o->opt) {
		if (req) {
			music_log(m, LOG_FATAL, "config: %s: unknown option", opt);
			return -1;
		} else {
			return 0;
		}
	}

	if (!o->arg) {
		if (*arg) {
			music_log(m, LOG_FATAL, "config: %s: unexpected argument", opt);
			return -1;
		}
	} else {
		if (!*arg) {
			music_log(m, LOG_FATAL, "config: %s: argument expected", opt);
			return -1;
		}
		if (o->arg==2) {
			char *end;
			errno = 0;
			strtol(arg, &end, 0);
			if (*end || errno) {
				music_log(m, LOG_FATAL, "config: %s: %s: integer expected",
				          opt, arg);
				return -1;
			}
		}
	}

	return o->ret;
}



void music_retry_cached(const struct music_module *m) {
	const struct music_module *cache = m->core->next;
	if (cache && cache->type==MUSIC_CACHE && cache->retryCached) {
		const struct music_module *array[2] = { 0, 0 };
		array[0] = m;
		cache->retryCached(cache, array);
	}
}



char *music_strdup_realloc(char *old, const char *str) {
	size_t len = strlen(str) + 1;
	old = realloc(old, len);
	memcpy(old, str, len);
	return old;
}



int music_sleep(const struct music_module *m, unsigned long mili) {
	int ret = 0;

#ifdef HAVE_POLL
	struct pollfd fd = { 0, POLLIN, 0 };
	if (!mili) return 0;

	fd.fd = sleep_pipe_fd;
	while (mili > INT_MAX && !(ret = poll(&fd, 1, mili))) {
		mili -= INT_MAX;
	}
	ret = ret ? ret : poll(&fd, 1, mili);
#else
	struct timeval timeout;
	fd_set set;
	if (!mili) return 0;

	timeout.tv_sec  = mili / 1000;
	timeout.tv_usec = (mili % 1000) * 1000;

	FD_ZERO(&set);
	FD_SET(sleep_pipe_fd, &set);
	ret = select(sleep_pipe_fd + 1, &set, 0, 0, &timeout);
#endif

	if (ret==-1) {
#ifdef HAVE_POLL
		music_log_errno(m, LOG_WARNING, "poll");
#else
		music_log_errno(m, LOG_WARNING, "select");
#endif
		return -1;
	} else {
		return !ret;
	}
}



int  music_run_once_check(void (*func)(void), void *arg) {
	static struct func_slist {
		struct func_slist *next;
		void (*func)(void);
		void *arg;
	} *first = 0;

	struct func_slist *el = first;

	while (el && (el->func!=func || el->arg!=arg)) el = el->next;
	if (el) {
		return 0;
	}

	el = malloc(sizeof *el);
	el->next = first;
	el->func = func;
	el->arg  = arg ;
	first = el;
	return 1;
}



void  music_song(const struct music_module *m, const struct music_song *song) {
	struct music_module *core = m->core;
	struct config *cfg  = core->data;

	struct slist *el;
	struct music_song *sng;

	const char *error = 0;

	if (!song->title) {
		error = " (no title)";
	} else if (song->length<30) {
		error = " (song too short)";
	}

#define OR(x, y) ((x) ? (x) : (y))
	music_log(m, error ? LOG_NOTICE : LOG_DEBUG,
	          "%s song: %s <%s> %s [%u sec]%s", error ? "ignoring" : "got",
	          OR(song->artist, "(null)"), OR(song->album , "(null)"),
	          OR(song->title , "(null)"), song->length, OR(error, ""));
#undef OR
	if (error) return;

	/* Copy song */
#define DUP(x) ((x) ? music_strdup_realloc(0, (x)) : 0)
	sng = malloc(sizeof *sng);
	sng->title   = DUP(song->title);
	sng->artist  = DUP(song->artist);
	sng->album   = DUP(song->album);
	sng->genre   = DUP(song->genre);
	sng->time    = song->time;
	sng->endTime = song->endTime;
	sng->length  = song->length;
#undef DUP

	el = malloc(sizeof *el);
	el->ptr = (void*)sng;

	pthread_mutex_lock(&cfg->songs.mutex);
	el->next = cfg->songs.first;
	cfg->songs.first = el;
	pthread_cond_signal(&cfg->songs.cond);
	pthread_mutex_unlock(&cfg->songs.mutex);

	return;
}
