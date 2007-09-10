/*
 * "Listening to" daemon dummy input module
 * $Id: in_dummy.c,v 1.5 2007/09/10 17:51:01 mina86 Exp $
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


static int   module_start(const struct module *m) __attribute__((nonnull));
static void  module_stop (const struct module *m) __attribute__((nonnull));
static void *module_run  (void *ptr)        __attribute__((nonnull));

struct config {
	pthread_t thread;
};


struct module *init(const char *name, const char *arg) {
	struct config *cfg;
	struct module *const m = malloc(sizeof *m + sizeof *cfg);
	(void)(name = name); /* supress warning */
	(void)(arg = arg); /* supress warning */

	m->type        = MUSIC_IN;
	m->start       = module_start;
	m->stop        = module_stop;
	m->free        = music_module_free;
	m->config      = 0;
	m->song.send   = 0;
	m->retryCached = 0;
	m->name        = 0;
	cfg = m->data  = m + 1;

	cfg->thread = 0;

	return m;
}


static int  module_start(const struct module *m) {
	struct config *const cfg = m->data;
	if (pthread_create(&cfg->thread, 0, module_run, (void*)m)) {
		music_log_errno(m, LOG_FATAL, "pthread_create");
		return 0;
	}
	return 1;
}


static void module_stop (const struct module *m) {
	struct config *const cfg = m->data;
	pthread_join(cfg->thread, 0);
}


static void *module_run  (void *ptr) {
	static struct song song = {
		"Title",
		"Artist",
		"Album",
		"Genre",
		0,
		0,
		60
	};
	while (music_running && music_sleep(ptr, 10000)) {
		song.endTime = time(&song.time) + 30;
		music_song(ptr, &song);
	}
	return 0;
}
