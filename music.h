#ifndef MUSIC_H
#define MUSIC_H


#ifndef __GNUC__
# define __attribute__(x)
# if __STDC_VERSION__ >= 199901L
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
	int time;
	int length;
};


struct music_option {
	const char *opt;
	int arg; /* 0 - none, 1 - string, 2 - long int */
	int ret;
};

struct module;

struct module_functions {
	int  (*start)(struct module *m);
	void (*stop)(struct module *m);
	int  (*putSong)(struct module *m, const struct song *song);
	int  (*configLine)(struct module *m, const char *opt,
	                   const char *arg);
	int  (*configEnd)(struct module *m);
};

struct module {
	const struct module_functions *f; /* functions */
	char *name;                       /* if overwritten must be free()d */
	struct module *core, *next;       /* for music's use */
	void *data;                       /* for module's use */
};



#define LOG_FATAL    0
#define LOG_ERROR    4
#define LOG_WARNING  8
#define LOG_NOTICE  12
#define LOG_DEBUG   16


void  music_log   (struct module *m, unsigned level, const char *fmt, ...)
	__attribute__((format (printf, 3, 4), nonnull, visibility("default")));
int   music_config(struct module *m, const struct music_option *options,
                   const char *opt, const char *arg, int req)
	__attribute__((nonnull, visibility("default")));
void  music_song  (struct module *m, const struct song *song)
	__attribute__((nonnull, visibility("default")));


char *music_strdup_realloc(char *old, const char *str);

static __inline__ char *music_strdup(const char *str) {
	return music_strdup_realloc(0, str);
}



#ifdef MUSIC_NO_MODULE
struct module *init(void)
	__attribute__((nonnull, visibility("default")));
#endif


#endif
