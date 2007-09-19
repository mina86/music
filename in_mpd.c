/*
 * "Listening to" daemon MPD input module
 * $Id: in_mpd.c,v 1.6 2007/09/19 00:10:32 mina86 Exp $
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

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "music.h"
#include "libmpdclient.h"



static int   module_start(const struct module *m) __attribute__((nonnull));
static void  module_stop (const struct module *m) __attribute__((nonnull));
static void  module_free (struct module *m) __attribute__((nonnull));
static int   module_conf (const struct module *m, const char *opt,
                          const char *arg)
	__attribute__((nonnull(1)));
static void *module_run  (void *ptr)        __attribute__((nonnull));


struct config {
	pthread_t thread;
	char *host, *password;
	long port;
};



struct module *init(const char *name, const char *arg) {
	struct config *cfg;
	struct module *const m = malloc(sizeof *m + sizeof *cfg);
	(void)name; /* supress warning */
	(void)arg;  /* supress warning */

	m->type        = MUSIC_IN;
	m->start       = module_start;
	m->stop        = module_stop;
	m->free        = module_free;
	m->config      = module_conf;
	m->song.send   = 0;
	m->retryCached = 0;
	m->name        = 0;
	cfg = m->data  = m + 1;

	cfg->thread = 0;

	cfg->host = music_strdup("localhost");
	cfg->password = 0;
	cfg->port = 6600;

	return m;
}


/****************************** Start ******************************/
static int   module_start(const struct module *m) {
	struct config *const cfg = m->data;
	if (pthread_create(&cfg->thread, 0, module_run, (void*)m)) {
		music_log_errno(m, LOG_FATAL, "pthread_create");
		return 0;
	}
	return 1;
}



/****************************** Stop ******************************/
static void  module_stop (const struct module *m) {
	struct config *cfg = m->data;
	pthread_join(cfg->thread, 0);
}



/****************************** Free ******************************/
static void  module_free (struct module *m) {
	struct config *const cfg = m->data;
	free(cfg->host);
	free(cfg->password);
	music_module_free(m);
}



/****************************** Configuration ******************************/
static int   module_conf (const struct module *m,
                          const char *opt, const char *arg) {
	static const struct music_option options[] = {
		{ "host",     1, 1 },
		{ "port",     2, 2 },
		{ "password", 1, 3 },
		{ 0, 0, 0 }
	};
	struct config *const cfg = m->data;
	if (!opt) return 1;

	switch (music_config(m, options, opt, arg, 1)) {
	case 1:
		cfg->host = music_strdup_realloc(cfg->host, arg);
		break;
	case 2:
		cfg->port = atol(arg);
		break;
	case 3:
		cfg->password = music_strdup_realloc(cfg->password, arg);
		break;
	default:
		return 0;
	}

	return 1;
}



/****************************** Run ******************************/
static mpd_Connection *module_do_connect(struct module *m)
	__attribute__((nonnull));
static void module_do_songs(struct module *m, mpd_Connection *conn)
	__attribute__((nonnull));
static int  module_do_submit_song(struct module *m, mpd_Connection *conn,
                                  int start) __attribute__((nonnull));



static void *module_run  (void *ptr) {
	struct module *const m = ptr;

	do {
		mpd_Connection *conn = module_do_connect(m);
		module_do_songs(m, conn);

		if (conn->error) {
			music_log(m, LOG_WARNING, "connection error: %s", conn->errorStr);
		}
		mpd_closeConnection(conn);

	} while (music_running);

	return 0;
}



static mpd_Connection *module_do_connect(struct module *m) {
	struct config *const cfg = m->data;
	unsigned delay = 5000;

	do {
		mpd_Connection *conn = mpd_newConnection(cfg->host, cfg->port, 10);

		if (!conn->error && cfg->password && *cfg->password) {
			mpd_sendPasswordCommand(conn, cfg->password);
			if (!conn->error) mpd_finishCommand(conn);
		}

		if (!conn->error) {
			return conn;
		}

		music_log(m, LOG_WARNING, "unable to connect to MPD: %s"
		          "; waiting %ds to reconnect", conn->errorStr, delay);
		mpd_closeConnection(conn);
		music_sleep(m, delay * 1000);
		if (delay<300000) delay <<= 1;
	} while (music_running);
	return 0;
}



static void module_do_songs(struct module *m, mpd_Connection *conn) {
	int id = -1, count = 0, start;

	while (music_running) {
		mpd_Status *status;
		int state, i;

		sleep(1);

		mpd_sendStatusCommand(conn);  if (conn->error) return;
		status = mpd_getStatus(conn); if (conn->error) return;
		state = status->state;
		i = status->songid;
		mpd_freeStatus(status);
		mpd_nextListOkCommand(conn);  if (conn->error) return;

		if (state!=MPD_STATUS_STATE_PLAY) continue;

		if (i!=id) {
			id = i;
			count = 1;
			start = time(0);
		} else if (count!=30 && ++count==30) {
			if (!module_do_submit_song(m, conn, start)) return;
		}
	}
}



static int  module_do_submit_song(struct module *m, mpd_Connection *conn,
                                  int start) {
	mpd_InfoEntity *info;
	struct song song;

	mpd_sendCurrentSongCommand(conn);      if (conn->error) return 0;
	info = mpd_getNextInfoEntity(conn);    if (!info) return 0;
	if (info->type!=MPD_INFO_ENTITY_TYPE_SONG) {
		mpd_freeInfoEntity(info);
		return 0;
	}

	song.title  = info->info.song->title;
	song.artist = info->info.song->artist;
	song.album  = info->info.song->album;
	song.genre  = info->info.song->genre;
	song.length = info->info.song->time < 1 ? 1 : info->info.song->time;
	song.time   = start;

	music_song(m, &song);

	mpd_freeInfoEntity(info);
	return 1;
}
