#ifndef MUSIC_H
#define MUSIC_H

#include "config.h"

#include <pthread.h>
#include <signal.h>


#ifndef __GNUC__
# define __attribute__(x)
# if __STDC_VERSION__ + 0 >= 199901L
#  define __inline__ inline
# else
#  define __inline__
# endif
#endif


struct song {
	const char *title;
	const char *artist;
	const char *album;
	const char *genre;
	time_t time;
	time_t endTime;
	unsigned length;
};


struct cache_entry {
	unsigned id;
	const char **modules;
	struct song song;
};


struct music_option {
	const char *opt;
	int arg; /* 0 - none, 1 - string, 2 - long int */
	int ret;
};


struct module {
	/* Module type.  init() must initialise it.  MUSIC_CORE is
	   reserved for core though. */
	enum { MUSIC_CORE = -1, MUSIC_IN, MUSIC_OUT, MUSIC_CACHE } type;


	/* Function pointers.  init() must initialise those pointers which
	   are designed for particular module type.  Moreover, no all
	   function have to be set to non-null.  */

	/* Executed once when starting module.  Have to return non-zero on
	   success and zero on failure.  If starting failed stop() won't
	   be called.  Required. */
	int  (*start)(const struct module *m);

	/* Executed once when stopping module.  Required.  */
	void (*stop)(const struct module *m);

	/* Executed once to free memory.  It should free all memory used
	   by module but not the module structure itself -- this will be
	   done by core.  Required if module allocates aditional memory
	   during configuration. */
	void (*free)(struct module *m);

	/* After module is loaded, executed for each configuration line
	   plus once when config for particular module has ended executed
	   with opt and arg NULL.  */
	int  (*config)(const struct module *m, const char *opt, const char *arg);

	union {
		/* For out modules executed when there is a song or songs to
		   submit.  Module must try to submit all songs and return
		   number of songs which it *failed* to submit.  Moreover,
		   errorPositions array must be filled with indexes of songs
		   which failed to be submitted.  in and cache modules have to
		   have this function NULL. */
		size_t (*send)(const struct module *m, size_t count,
		               const struct song *const *songs,
		               size_t *errorPositions);

		/* For cache modules executed when there is a song that needs to
		   be cached.  For in and out modules have to have this function
		   NULL. */
		void (*cache)(const struct module *m, const struct cache_entry *song);
	} song;

	/* Called once in a while to request cache to try to resend cached
	   songs.  Parameteres are a list of out modules to send to.  It
	   is cache module responsibility to call song.send functions and
	   interprete their result.  This is required for cache
	   modules. */
	void (*retryCached)(const struct module *m, size_t count,
	                    const struct module *modules);


	/* Those two are for internal use by core.  init() (or module in
	   any other place) should not touch them. */
#ifdef MUSIC_NOT_MODULE
	struct module *next, *core;
#else
	char internal[sizeof(struct module*[2])];
#endif


	/* This is a module name (not neccesary file name).  It may be set
	   by user using name configuration option.  init() must
	   initialise it to 0.  Module can update it (but should not
	   really) when config() is called with NULL opt and arg (that is
	   at the end of config section for given module) but have to
	   rememver to either free() or realloc() current name. */
	char *name;

	/* This is for use by module.  Core will never touch this
	   thing. */
	void *data;
};



#ifdef MUSIC_NOT_MODULE
extern volatile int music_running;
extern int sleep_pipe_fd;
#else
extern const volatile int music_running;
extern const int sleep_pipe_fd;
#endif


enum {
	LOG_FATAL   =  0,
	LOG_ERROR   =  4,
	LOG_WARNING =  8,
	LOG_NOTICE  = 12,
	LOG_DEBUG   = 16
};

void  music_log   (const struct module *m, unsigned level,
                   const char *fmt, ...)
	__attribute__((format (printf, 3, 4), nonnull, visibility("default")));
void  music_log_errno(const struct module *m, unsigned level,
                      const char *fmt, ...)
	__attribute__((format (printf, 3, 4), nonnull, visibility("default")));
int   music_config(const struct module *m, const struct music_option *options,
                   const char *opt, const char *arg, int req)
	__attribute__((nonnull, visibility("default")));
void  music_song  (const struct module *m, const struct song *song)
	__attribute__((nonnull, visibility("default")));

char *music_strdup_realloc(char *old, const char *str);
#define music_strdup(x) music_strdup_realloc(0, x)

int music_sleep(const struct module *m, unsigned long mili)
	__attribute__((nonnull, visibility("default")));


void music_retry_cached(const struct module *m)
	__attribute__((nonnull, visibility("default")));


void music_module_free(struct module *m);
void music_module_free_and_data(struct module *m);


#ifndef MUSIC_NOT_MODULE
struct module *init(const char *name, const char *arg)
	__attribute__((nonnull, visibility("default")));
#endif


#endif
