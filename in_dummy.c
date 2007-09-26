/*
 * "Listening to" daemon dummy input module
 * $Id: in_dummy.c,v 1.11 2007/09/26 22:23:53 mina86 Exp $
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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "music.h"


/**
 * Starts module.  See music_module::start.
 *
 * @param m in_mpd module to start.
 * @return whether starting succeed.
 */
static int   module_start(const struct music_module *restrict m)
	__attribute__((nonnull));


/**
 * Stops module.  See music_module::stop.
 *
 * @param m in_mpd module to stop.
 */
static void  module_stop (const struct music_module *restrict m)
	__attribute__((nonnull));


/**
 * Module's thread function.
 *
 * @param ptr a pointer to const struct music_module cast to pointer
 *            to void.
 * @return return value shall be ignored.
 */
static void *module_run  (void *restrict ptr) __attribute__((nonnull));



/**
 * Module's configuration.
 */
struct module_config {
	pthread_t thread;
};



struct music_module *init(const char *restrict name,
                          const char *restrict arg) {
	struct module_config *cfg;
	struct music_module *const m = music_init(MUSIC_IN, sizeof *cfg);
	(void)name; /* supress warning */
	(void)arg;  /* supress warning */

	m->start    = module_start;
	m->stop     = module_stop;
	cfg         = m->data;
	cfg->thread = 0;

	return m;
}



static int  module_start(const struct music_module *restrict m) {
	struct module_config *const cfg = m->data;
	if (pthread_create(&cfg->thread, 0, module_run, (void*)m)) {
		music_log_errno(m, LOG_FATAL, "pthread_create");
		return 0;
	}
	return 1;
}



static void module_stop (const struct music_module *restrict m) {
	struct module_config *const cfg = m->data;
	pthread_join(cfg->thread, 0);
}



static void *module_run  (void *restrict ptr) {
	static struct music_song song = {
		"Title",
		"Artist",
		"Album",
		"Genre",
		0,
		0,
		60
	};
	while (music_running && music_sleep(ptr, 10000)==1) {
		song.endTime = time(&song.time) + 30;
		song.time -= 30;
		music_song(ptr, &song);
	}
	return 0;
}
