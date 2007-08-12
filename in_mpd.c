#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "music.h"
#include "libmpdclient.h"



static int   module_start(struct module *m) __attribute__((nonnull));
static void  module_stop (struct module *m) __attribute__((nonnull));
static int   module_conf (struct module *m, const char *opt, const char *arg)
	__attribute__((nonnull));
static void *module_run  (void *ptr)        __attribute__((nonnull));


static struct module_functions functions = {
	module_start,
	module_stop,
	0,
	module_conf,
	0
};


struct config {
	pthread_t thread;
	char *host, *password;
	long port;
};



struct module *init() {
	struct config *cfg;
	struct module *const m = malloc(sizeof *m + sizeof *cfg);

	m->f = &functions;
	m->name = 0;
	m->core = m->next = 0;
	cfg = m->data = m + 1;

	cfg->thread = 0;

	cfg->host = music_strdup("localhost");
	cfg->password = 0;
	cfg->port = 6600;

	return m;
}


/****************************** Start ******************************/
static int   module_start(struct module *m) {
	struct config *const cfg = m->data;
	if (pthread_create(&cfg->thread, 0, module_run, m)) {
		music_log_errno(m, LOG_FATAL, "pthread_create");
		return 1;
	}
	return 0;
}



/****************************** Stop ******************************/
static void  module_stop (struct module *m) {
	struct config *cfg = m->data;
	pthread_join(cfg->thread, 0);
}



/****************************** Configuration ******************************/
static int   module_conf (struct module *m, const char *opt, const char *arg) {
	static const struct music_option options[] = {
		{ "host",     1, 1 },
		{ "port",     2, 2 },
		{ "password", 1, 3 },
		{ 0, 0, 0 }
	};
	struct config *const cfg = m->data;

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
		/* Dead code */
		return 1;
	}

	return 0;
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
