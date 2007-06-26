#include <errno.h>
#include <pthread.h>
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
	0,
	0,
	0
};


struct config {
	pthread_t thread;
	volatile int running;  /* not implemented */
};


struct module *init() {
	struct config *cfg;
	struct module *const m = malloc(sizeof *m + sizeof *cfg);

	m->f = &functions;
	m->name = 0;
	m->core = m->next = 0;
	cfg = m->data = m + 1;

	cfg->thread = 0;
	cfg->running = 1;

	return m;
}


static int  module_start(struct module *m) {
	struct config *const cfg = m->data;
	if (pthread_create(&cfg->thread, 0, module_run, m)) {
		music_log(m, LOG_FATAL, "pthread_create: %s", strerror(errno));
		return 1;
	}
	return 0;
}


static void module_stop (struct module *m) {
	/* FIXME: Doesn't take adventage of 'running'; simply cancels
	   thread whereas should wake it up and wait for it to close
	   itself. */
	struct config *const cfg = m->data;
	cfg->running = 0;
	pthread_cancel(cfg->thread);
	pthread_join(cfg->thread, 0);
}


static void *module_run  (void *ptr) {
	struct module *const m = ptr;
	static struct song song = {
		"Title",
		"Artist",
		"Album",
		"Genre",
		0,
		60
	};
	volatile int *const running = &((struct config*)m->data)->running;
	while (*running) {
		sleep(30 + rand() % 30);
		song.time = time(0);
		music_song(m, &song);
	}
	return 0;
}
