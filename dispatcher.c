/*
 * "Listening to" daemon songs dispatcher.
 * $Id: dispatcher.c,v 1.3 2007/09/26 22:24:08 mina86 Exp $
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

#include <limits.h>
#include <stdlib.h>
#include <string.h>


/**
 * Starts module.  See music_module::start.
 *
 * @param m dispatcher module to start.
 * @return whether starting succeed.
 */
static int   module_start(const struct music_module *restrict m)
	__attribute__((nonnull));


/**
 * Stops module.  See music_module::stop.
 *
 * @param m dispatcher module to stop.
 */
static void  module_stop (const struct music_module *restrict m)
	__attribute__((nonnull));


/**
 * Adds song to queue.  See music_module::song::cache but note that it
 * behaves a bit differently.  In particular -- modules is
 * ignored.
 *
 * @param m dispatcher module to stop.
 * @param song the song songs.
 * @param modules ignored.
 * @return undefined value.
 */
static void  module_cache(const struct music_module *restrict m,
                          const struct music_song *restrict song,
                          const struct music_module *restrict const *restrict modules)
	__attribute__((nonnull(1, 2)));


/**
 * Module's thread function.  Run if there is a cache module.
 *
 * @param ptr a pointer to const struct music_module cast to pointer
 *            to void.
 * @return return value shall be ignored.
 */
static void *module_run_cache   (void *restrict ptr) __attribute__((nonnull));


/**
 * Module's thread function.  Run if there is no cache module.
 *
 * @param ptr a pointer to const struct music_module cast to pointer
 *            to void.
 * @return return value shall be ignored.
 */
static void *module_run_no_cache(void *restrict ptr) __attribute__((nonnull));



/**
 * A linked list element used to store songs.
 */
struct slist {
	struct slist *next;      /**< Next element. */
	struct music_song song;  /**< Song. */
};



/**
 * Configuration for songs disptcher.
 */
struct dispatcher_config {
	pthread_t thread;       /**< Song dispatcher's thread ID/ */
	pthread_mutex_t mutex;  /**< Mutex used by song dispatcher. */
	pthread_cond_t cond;    /**< Song dispatcher's condition variable. */
	struct slist *first;    /**< First song on songs queue. */
	size_t count;           /**< Number of songs in queue. */
	size_t outCount;        /**< Number of otput modules. */
};


/**
 * Frees a single linked list and all song's data.
 *
 * @param first linked list first element or NULL.
 */
static void slist_free(struct slist *first) {
	struct slist *tmp;
	for (; first; first = tmp) {
		tmp = first->next;
		free((char*)first->song.title );
		free((char*)first->song.artist);
		free((char*)first->song.album );
		free((char*)first->song.genre );
		free(first);
	}
}



/**
 * Initialises dispatcher module.
 *
 * @return dispatcher module or NULL on error.
 */
struct music_module *dispatcher_init() {
	struct dispatcher_config *cfg;
	struct music_module *const m = music_init(MUSIC_CORE, sizeof *cfg);

	m->start       = module_start;
	m->stop        = module_stop;
	m->song.cache  = module_cache;
	cfg            = m->data;
	cfg->thread    = 0;
	cfg->first     = 0;
	cfg->count     = 0;
	cfg->outCount  = 0;
	pthread_mutex_init(&cfg->mutex, 0);
	pthread_cond_init (&cfg->cond, 0);

	return m;
}



static int   module_start(const struct music_module *restrict m) {
	struct dispatcher_config *const cfg = m->data;
	struct music_module *o = m->next;
	size_t i = 0;
	int ret;

	if (!o || o->type!=MUSIC_OUT) {
		music_log(m, LOG_FATAL, "no output modules");
		return 0;
	}

	for (; o && o->type==MUSIC_OUT; o = o->next) {
		i += !!o->song.send;
	}
	if (!(cfg->outCount = i)) {
		music_log(m, LOG_FATAL, "no output modules with send method set");
		return 0;
	}

	if (m->core->next!=m) {
		ret = pthread_create(&cfg->thread, 0, module_run_cache, (void*)m);
	} else {
		ret = pthread_create(&cfg->thread, 0, module_run_no_cache, (void*)m);
	}

	if (ret) {
		music_log_errno(m, LOG_FATAL, "pthread_create");
		return 0;
	}
	return 1;
}



static void  module_stop (const struct music_module *restrict m) {
	struct dispatcher_config *cfg = m->data;
	pthread_mutex_lock(&cfg->mutex);
	pthread_cond_signal(&cfg->cond);
	pthread_mutex_unlock(&cfg->mutex);
	pthread_join(cfg->thread, 0);
	pthread_mutex_destroy(&cfg->mutex);
	pthread_cond_destroy(&cfg->cond);

	slist_free(cfg->first);
}



static void  module_cache(const struct music_module *restrict m,
                          const struct music_song *restrict song,
                          const struct music_module *restrict const *restrict modules){
	struct dispatcher_config *const cfg = m->data;
	struct slist *el = malloc(sizeof *el);
	(void)modules;

	if (!music_running || !cfg->thread) return;

#define DUP(x) ((x) ? music_strdup_realloc(0, (x)) : 0)
	el->song.title   = DUP(song->title  );
	el->song.artist  = DUP(song->artist );
	el->song.album   = DUP(song->album  );
	el->song.genre   = DUP(song->genre  );
#undef DUP
	el->song.time    = song->time   ;
	el->song.endTime = song->endTime;
	el->song.length  = song->length ;

	pthread_mutex_lock(&cfg->mutex);
	el->next = cfg->first;
	cfg->first = el;
	++cfg->count;
	pthread_cond_signal(&cfg->cond);
	pthread_mutex_unlock(&cfg->mutex);

	return;
}



static void *module_run_no_cache(void *restrict ptr) {
	const struct music_module *const m = ptr, *o;
	struct dispatcher_config *const cfg = m->data;
	const size_t outCount = cfg->outCount;
	struct music_song **songs, **s;
	struct slist *el, *first;
	size_t i;

	do {
		pthread_mutex_lock(&cfg->mutex);
		if (!cfg->first) {
			pthread_cond_wait(&cfg->cond, &cfg->mutex);
		}
		el = first = cfg->first;
		i = cfg->count;
		cfg->first = 0;
		cfg->count = 0;
		pthread_mutex_unlock(&cfg->mutex);

		if (!music_running) {
			break;
		}

		s = songs = malloc((i + 1) * sizeof *songs);
		for (; el; el = el->next) *s++ = &el->song;
		*s = 0;

		i = outCount;
		for (o = m->next; i; o = o->next, --i) {
			if (o->song.send) {
				o->song.send(o, (const struct music_song **)songs, 0);
			}
		}

		free(songs);
		slist_free(first);
		first = 0;
	} while (music_running);

	slist_free(first);
	return 0;
}



static void submit_songs_and_cache(const struct music_module *restrict m,
                                   const struct music_song *restrict const *restrict songs,
                                   size_t count, uint_least32_t *restrict flags,
                                   const struct music_module *restrict *restrict outs)
	__attribute__((nonnull));



static void *module_run_cache  (void *restrict ptr) {
	const struct music_module *const m = ptr;
	struct dispatcher_config *const cfg = m->data;

	size_t i = cfg->outCount;
	const struct music_module **const outs = malloc((i*2+1) * sizeof *outs);

	uint_least32_t *flags = malloc(i);
	const struct music_song *songs[33];
	struct slist *first = 0, *el;


	{
		const struct music_module *o = m, **p = outs;
		do {
			if ((o = o->next)->song.send) *p++ = o;
		} while (--i);
	}


	do {
		pthread_mutex_lock(&cfg->mutex);
		if (!cfg->first) {
			pthread_cond_wait(&cfg->cond, &cfg->mutex);
		}
		first = cfg->first;
		cfg->first = 0;
		cfg->count = 0;
		pthread_mutex_unlock(&cfg->mutex);

		if (!music_running) {
			break;
		}

		el = first;
		do {
			for (i = 0; el && i<32; el = el->next) songs[i++] = &el->song;
			songs[i] = 0;
			submit_songs_and_cache(m, songs, i, flags, outs);
		} while (el);

		slist_free(first);
		first = 0;
	} while (music_running);


	slist_free(first);
	return 0;
}



static void submit_songs_and_cache(const struct music_module *restrict m,
                                   const struct music_song *restrict const *restrict songs,
                                   size_t count, uint_least32_t *restrict flags,
                                   const struct music_module *restrict *restrict outs) {
	struct dispatcher_config *const cfg = m->data;
	const size_t outCount = cfg->outCount;
	const struct music_module *restrict *const oarr = outs+outCount;
	const struct music_module *restrict *p;
	const struct music_song *restrict const *s;
	uint_least32_t mask;
	size_t i;

	memset(flags, 0, cfg->outCount * sizeof *flags);


	/*
	 * The problem here is as follows.  When we submit songs we tell
	 * module X to submit given set of songs alpha, beta, gamma...
	 * But then, when it comes to caching, we need to tell cache
	 * module to cache song alpha for modules A, B, C...  Because of
	 * that we keep a 2D matrix of flags which specify whether module
	 * X suceed in submitting song alpha.  When we submit songs we
	 * mark (for each module) songs that failed to be submitted and
	 * then when caching we check (for each song) which module failed
	 * to submit it.
	 */


	/* Submit songs */
	for (i = 0; i < outCount; ++i) {
		size_t errPos[32];
		int ret = outs[i]->song.send(outs[i], songs, errPos);

		if (ret<0 || (size_t)ret >= count) {
			flags[i] = 0xffffffff;
		} else if (ret) {
			while (ret) flags[i] |= ((uint_least32_t)1) << (errPos[--ret]&31);
		}
	}


	/* Cache songs */
	for (mask = 1, s = songs; *s; mask <<= 1, ++s) {
		p = oarr;
		for (i = 0; i < outCount; ++i) {
			if (flags[i] & mask) *p++ = outs[i];
		}
		if (p==oarr) continue;
		*p = 0;

		m->core->next->song.cache(m->core->next, *s, oarr);
	}
}
