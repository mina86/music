#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "music.h"


static int   module_start(struct module *m) __attribute__((nonnull));
static void  module_stop (struct module *m) __attribute__((nonnull));
static void *module_run  (void *ptr)        __attribute__((nonnull));


static struct module_functions functions = {
	module_start,
	module_stop,
	0, 0, 0, 0, 0, 0, 0
};


struct config {
	pthread_t thread;
};


struct module *init(const char *name, const char *arg) {
	struct config *cfg;
	struct module *const m = malloc(sizeof *m + sizeof *cfg);
	(void)(name = name); /* supress warning */
	(void)(arg = arg); /* supress warning */

	m->f = &functions;
	m->name = 0;
	cfg = m->data = m + 1;

	cfg->thread = 0;

	return m;
}


static int  module_start(struct module *m) {
	struct config *const cfg = m->data;
	if (pthread_create(&cfg->thread, 0, module_run, m)) {
		music_log_errno(m, LOG_FATAL, "pthread_create");
		return 1;
	}
	return 0;
}


static void module_stop (struct module *m) {
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
