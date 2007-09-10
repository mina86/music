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
