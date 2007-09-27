/*
 * "Listening to" daemon MPD input module
 * $Id: in_mpd.c,v 1.12 2007/09/27 21:37:41 mina86 Exp $
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
 * Frees memory allocated by module.  See music_module::free.
 *
 * @param m in_mpd module to free.
 */
static void  module_free (struct music_module *restrict m)
	__attribute__((nonnull));


/**
 * Accepts configuration options.  See music_module::conf.
 *
 * @param m in_mpd module.
 * @param opt option keyword.
 * @param arg argument.
 * @return whether option was accepted.
 */
static int   module_conf (const struct music_module *restrict m,
                          const char *restrict opt, const char *restrict arg)
	__attribute__((nonnull(1)));


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
	pthread_t thread;      /**< Thread's ID module is running. */
	char *host;            /**< Host to connect to. */
	char *password;        /**< Password to use when connecting. */
	long port;             /**< Port to connect to. */
};



struct music_module *init(const char *restrict name,
                          const char *restrict arg) {
	struct module_config *cfg;
	struct music_module *const m = music_init(MUSIC_IN, sizeof *cfg);
	(void)name; /* supress warning */
	(void)arg;  /* supress warning */

	m->start      = module_start;
	m->stop       = module_stop;
	m->free       = module_free;
	m->config     = module_conf;
	cfg           = m->data;
	cfg->thread   = 0;
	cfg->host     = music_strdup("localhost");
	cfg->password = 0;
	cfg->port     = 6600;

	return m;
}



static int   module_start(const struct music_module *restrict m) {
	struct module_config *const cfg = m->data;
	if (pthread_create(&cfg->thread, 0, module_run, (void*)m)) {
		music_log_errno(m, LOG_FATAL, "pthread_create");
		return 0;
	}
	return 1;
}




static void  module_stop (const struct music_module *restrict m) {
	struct module_config *cfg = m->data;
	pthread_join(cfg->thread, 0);
}




static void  module_free (struct music_module *restrict m) {
	struct module_config *const cfg = m->data;
	free(cfg->host);
	free(cfg->password);
}




static int   module_conf (const struct music_module *restrict m,
                          const char *restrict opt,
                          const char *restrict arg) {
	static const struct music_option options[] = {
		{ "host",     1, 1 },
		{ "port",     2, 2 },
		{ "password", 1, 3 },
		{ 0, 0, 0 }
	};
	struct module_config *const cfg = m->data;
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




/**
 * Connects to MPD.  This function runs in a loop which finishes
 * either when it menages to connect to MPD or when music_running is
 * set to zero (which means that application is terminating).
 *
 * @param m in_mpd module.
 * @return connection to MPD or NULL on error.
 */
static mpd_Connection *module_do_connect(const struct music_module *restrict m)
	__attribute__((nonnull));


/**
 * Retrives song MPD is playing and submitts it if needed.  This
 * function runs in a loop which finishes either when there is
 * a connection error or when music_running is set to zero (which
 * means taht application is terminating).
 *
 * @param m in_mpd module.
 * @param conn connection to MPD.
 */

static void module_do_songs(const struct music_module *restrict m,
                            mpd_Connection *conn) __attribute__((nonnull));


/**
 * Retrives song MPD is playing and submits it to core using
 * music_song() function.
 *
 * @param m in_mpd module.
 * @param conn connection to MPD.
 * @param start when the song has started playing.
 * @param songid song's ID to retrive.
 * @return zero on error, non-zero on success.
 */
static int  module_do_submit_song(const struct music_module *restrict m,
                                  mpd_Connection *restrict conn,
                                  time_t start, int songid)
	__attribute__((nonnull));



static void *module_run  (void *restrict ptr) {
	const struct music_module *const m = ptr;

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



static mpd_Connection *module_do_connect(const struct music_module *restrict m) {
	struct module_config *const cfg = m->data;
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



static void module_do_songs(const struct music_module *restrict m,
                            mpd_Connection *conn) {
	int id = -1, count = 0;
	time_t start;

	while (music_sleep(m, 1000)==1) {
		mpd_Status *status;
		int state, i, elapsed;

		mpd_sendStatusCommand(conn);  if (conn->error) return;
		status = mpd_getStatus(conn); if (conn->error) return;
		state = status->state;
		i = status->songid;
		elapsed = status->elapsedTime;
		mpd_freeStatus(status);
		mpd_nextListOkCommand(conn);  if (conn->error) return;

		if (state!=MPD_STATUS_STATE_PLAY) continue;

		if (i!=id) {
			id = i;
			count = 1;
			start = time(0) - elapsed;
		} else if (count!=30 && ++count==30) {
			if (!module_do_submit_song(m, conn, start, id)) return;
		}
	}
}



static int  module_do_submit_song(const struct music_module *restrict m,
                                  mpd_Connection *restrict conn,
                                  time_t start, int songid) {
	mpd_InfoEntity *info;
	struct music_song song;

	/*	mpd_sendCurrentSongCommand(conn);      if (conn->error) return 0; */
	mpd_sendPlaylistIdCommand(conn, songid); if (conn->error) return 0;
	info = mpd_getNextInfoEntity(conn);      if (!info) return 0;
	if (info->type!=MPD_INFO_ENTITY_TYPE_SONG) {
		mpd_freeInfoEntity(info);
		return 0;
	}

	song.title   = info->info.song->title;
	song.artist  = info->info.song->artist;
	song.album   = info->info.song->album;
	song.genre   = info->info.song->genre;
	song.length  = info->info.song->time < 1 ? 1 : info->info.song->time;
	song.time    = start;
	song.endTime = song.length > 1 ? start + (time_t)song.length : -1;

	music_song(m, &song);

	mpd_freeInfoEntity(info);
	return 1;
}
