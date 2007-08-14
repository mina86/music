#define MUSIC_NOT_MODULE
#include "music.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef HAVE_POLL
# include <poll.h>
#else
# include <sys/select.h>
# include <sys/time.h>
#endif


volatile int music_running = 1;
int sleep_pipe_fd;


static int  config_line(struct module *m, const char *opt, const char *arg)
	__attribute__((nonnull));
static int  parse_line(char *buf, struct module **m_)
	__attribute__((nonnull));


static int  sig = 0;
static void got_sig(int signum);
static void ignore_sig(int signum);


struct config {
	pthread_mutex_t log_mutex;
	struct module *cache;
	char    *logfile;
	unsigned loglevel;
	unsigned logboth;
};



/****************************** Main ******************************/
int main(int argc, char **argv) {
	struct config cfg = { PTHREAD_MUTEX_INITIALIZER, 0, 0, LOG_NOTICE, 0 };
	struct module_functions functions = {
		0, 0, config_line, 0, 0, 0, 0, 0
	};
	struct module core = {
		0,
		(char*)"core",
		0,
		0,
		0
	}, *m;
	char *ch;
	int i;
	int pipe_fds[2];

	core.f = &functions;
	core.core = &core;
	core.data = &cfg;


	/* Get program name */
	for (core.name = ch = *argv; *ch; ++ch) {
		if ((*ch=='/' || *ch=='\\') && ch[1] && ch[1]!='/' && ch[1]!='\\') {
			core.name = ch + 1;
		}
	}


	/* Help */
	if (argc>=2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		fputs("usage: music [ config-file ... ]\n", stdout);
		return 0;
	}


	/* Read config */
	if (argc<2) {
		argv[1] = (char*)"-";
		argc = 2;
	}

	for (i = 1; i < argc; ++i) {
		FILE *fp = strcmp(argv[i], "-") ? fopen(argv[i], "r") : stdin;
		char buf[1026];

		if (!fp) {
			music_log_errno(&core, LOG_FATAL, "open: %s", argv[i]);
			return 1;
		}

		m = &core;
		if (ch) *ch = 0;
		while (fgets(buf, sizeof buf, fp)) {
			int result = parse_line(buf, &m);
			if (result) return result;
		}

		if (fp!=stdin) {
			fclose(fp);
		}
	}


	/* Open log file */
	if (cfg.logfile && *cfg.logfile) {
		i = open(cfg.logfile, O_WRONLY | O_APPEND | O_CREAT, 0600);
		if (i==-1) {
			music_log_errno(&core, LOG_FATAL, "open: %s", cfg.logfile);
			return 1;
		}
		fflush(stderr);
		dup2(i, 2); /* stderr is not logfile */
		close(i);
	}

	music_log(&core, LOG_NOTICE, "starting");
	cfg.logboth = 1;

	/* Daemonize */
	switch (fork()) {
	case -1:
		music_log_errno(&core, LOG_FATAL, "fork");
		return 1;
	case  0: break;
	default: return 0;
	}

	setsid();

	switch (fork()) {
	case -1:
		music_log_errno(&core, LOG_FATAL, "fork");
		return 1;
	case  0: break;
	default: return 0;
	}

	chdir("/");
	cfg.logboth = 0;
	i = sysconf(_SC_OPEN_MAX);
	while (--i > 2) {
		close(i);
	}
	close(0);
	open("/dev/null", O_RDWR); /* stdin  is /dev/null */
	fflush(stdout);
	dup2(0, 1);                /* stdout is /dev/null */


	/* Create sleep pipe */
	if (pipe(pipe_fds)) {
		music_log_errno(&core, LOG_FATAL, "pipe");
		return 1;
	}
	sleep_pipe_fd = pipe_fds[0];


	/* Register signal handler */
	signal(SIGHUP,  got_sig);
	signal(SIGINT,  got_sig);
	signal(SIGILL,  got_sig);
	signal(SIGQUIT, got_sig);
	signal(SIGSEGV, got_sig);
	signal(SIGTERM, got_sig);
	signal(SIGALRM, ignore_sig);


	/* Start everything */
	pthread_mutex_init(&cfg.log_mutex, 0);
	for (m = core.next; m; m = m->next) {
		struct module *m2;
		i = sig;
		if (!i) {
			music_log(m, LOG_NOTICE + 2, "starting");
			if (!m->f->start || !m->f->start(m)) continue;
			music_log(m, LOG_FATAL, "error starting module");
		}
		for (m2 = core.next; m2 != m; m2 = m2->next) {
			music_log(m, LOG_NOTICE + 2, "stopping");
			if (m2->f->stop) m2->f->stop(m2);
		}
		return i ? 0 : 1;
	}

	/* Wait for signal */
	while (music_running) {
		pause();
	}
	if (sig) {
		music_log(&core, LOG_NOTICE + 2, "got signal %d; exiting", sig);
	}

	/* Stop everything */
	write(pipe_fds[1], "B", 1);
	for (m = core.next; m; m = m->next) {
		music_log(m, LOG_NOTICE + 2, "stopping");
		if (m->f->stop) m->f->stop(m);
	}

	/* OS will free all resources we were using so no need to do it
	   ourselfves */
	music_log(&core, LOG_NOTICE, "terminated");
	return 0;
}



/****************************** Parse Line ******************************/
static int  parse_line(char *buf, struct module **m_) {
	struct module *m    = *m_;
	struct module *core = m->core;
	struct module *(*init)(const char *name, const char *arg);
	char *option, *moduleName, *argument, *ch, *end;
	void *handle;
	size_t len;


	/* Split line into option and arguments */
	for (option  = buf;         isspace(*option)         ; ++option);
	for (ch  = option ; *ch && !isspace(*ch ) && *ch!='#'; ++ch);
	for (end = ch     ;         isspace(*ch )            ; ++ch);
	*end = 0;
	if (!*option) return 0;
	end = ch;
	for (argument = ch; *ch                   && *ch!='#'; ++ch) {
		if (!isspace(*ch)) end = ch + 1;
	}
	*end = 0;
	len = end - argument;


	/* "name" argument */
	if (!strcmp(option, "name")) {
		if (m==core) {
			music_log(m, LOG_FATAL, "name: unknown option");
			return 1;
		} else if (!*argument) {
			music_log(m, LOG_FATAL, "name: argument expected");
			return 1;
		}
		m->name = realloc(m->name, len + 1);
		memcpy(m->name, argument, len + 1);
		return 0;
	}


	/* Pass arguments to module */
	if (strcmp(option, "module")) {
		if (m->f->configLine) {
			return m->f->configLine(m, option, argument);
		} else {
			music_log(m, LOG_FATAL, "%s: unknown option", option);
			return 1;
		}
	}


	/* Configuration for current module has ended */
	if (m->f->configEnd) {
		m->f->configEnd(m);
	}


	/* Module to load not specified */
	if (!*argument) {
		music_log(core, LOG_FATAL, "module: argument expected");
		return 1;
	}


	/* Split module name and argument */
	for (ch = moduleName = argument; *ch && !isspace(ch); ++ch);
	if (*ch) {
		len = ch - moduleName;
		*ch = 0;
		while (isspace(*++ch));
	}
	memcpy(moduleName = malloc(len + 1), argument, len + 1);
	argument = ch;


	/* Load module */
	music_log(core, LOG_NOTICE,
	          *argument ? "%s: loading module (%s)" : "%s: loading module",
	          moduleName, argument);

	sprintf(buf, "./%s.so", moduleName);
	handle = dlopen(buf, RTLD_LAZY);
	if (!handle) {
		music_log(core, LOG_FATAL, "%s", dlerror());
		free(moduleName);
		return 1;
	}

	/* Load "init" function */
	dlerror();
	if (!(handle = dlsym(handle, "init"))) {
		music_log(core, LOG_FATAL, "%s", dlerror());
		free(moduleName);
		return 1;
	}

	/* Run "init" function */
	*(void **)&init = handle;
	m = init(moduleName, argument);
	if (!m) {
		music_log(core, LOG_FATAL, "%s: init: unknown error", buf);
		free(moduleName);
		return 1;
	}

	/* Fill structure */
	if (m->name) free(m->name);
	m->name = moduleName;
	m->next = core->next;
	m->core = core;
	core->next = m;
	*m_ = m;
	return 0;
}



/****************************** Put song ******************************/
void  music_song(struct module *m, const struct song *song) {
	/*struct module *core = m->core;
	struct config *cfg  = core->data;*/

	const char *error = 0;

	if (!song->title) {
		error = " (no title)";
	} else if (song->length<30) {
		error = " (song too short)";
	}

	music_log(m, error ? LOG_NOTICE : LOG_DEBUG,
	          "%s song: %s <%s> %s [%u sec]%s",
	          error ? "ignoring" : "got",
	          song->artist ? song->artist : "(null)",
	          song->album  ? song->album  : "(null)",
	          song->title  ? song->title  : "(null)",
	          song->length, error ? error : "");
	return;
}



/****************************** Config Line ******************************/
static int  config_line(struct module *m, const char *opt, const char *arg) {
	static struct music_option options[] = {
		{ "logfile" , 1, 1 },
		{ "loglevel", 1, 2 },
		{ 0, 0, 0 }
	};
	struct config *const cfg = m->data;

	switch (music_config(m, options, opt, arg, 1)) {
	case -1:
	case  0: return 1;
	case  1: {
		size_t len = strlen(arg) + 1;
		cfg->logfile = realloc(cfg->logfile, len);
		memcpy(cfg->logfile, arg, len);
		break;
	}
	case  2:
		cfg->loglevel = atoi(arg);
		break;
	}
	return 0;
}



/****************************** Got Sig ******************************/
static void got_sig(int signum) {
	music_running = 0;
	if (!sig) {
		sig = signum;
	} else {
		abort();
	}
	signal(signum, got_sig);
}

static void ignore_sig(int signum) {
	signal(signum, ignore_sig);
}



/****************************** Music Log ******************************/
static void music_log_internal(struct module *m, unsigned level,
                               const char *fmt, va_list ap, int errStr);

void music_log (struct module *m, unsigned level, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	music_log_internal(m, level, fmt, ap, 0);
}

void music_log_errno(struct module *m, unsigned level, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	music_log_internal(m, level, fmt, ap, 1);
}


static void music_log_internal_do(FILE *stream, const char *date,
                                  const char *name,
                                  const char *fmt, va_list ap,
                                  const char *error)
	__attribute__((nonnull(1,2,3)));

static void music_log_internal(struct module *m, unsigned level,
                               const char *fmt, va_list ap, int errStr) {
	static char buf[32];
	struct config *cfg = m->core->data;
	va_list ap2;
	char *str;
	time_t t;

	if (cfg->loglevel < level) return;

	pthread_mutex_lock(&cfg->log_mutex);

	t = time(0);
	if (!strftime(buf, sizeof buf, "[%Y/%m/%d %H:%M:%S] ", gmtime(&t))) {
		buf[0] = 0;
	}

	str = errStr ? strerror(errno) : 0;

	if (cfg->logboth) {
#if defined __GNUC__ && defined __va_copy
		/* In case -pedantic -ansi was added */
		__va_copy(ap2, ap);
#else
		va_copy(ap2, ap);
#endif
		music_log_internal_do(stderr, buf, m->name, fmt, ap, str);
		music_log_internal_do(stdout, buf, m->name, fmt, ap2, str);
	} else {
		music_log_internal_do(stderr, buf, m->name, fmt, ap, str);
	}

	pthread_mutex_unlock(&cfg->log_mutex);
}


static void music_log_internal_do(FILE *stream, const char *date,
                                  const char *name,
                                  const char *fmt, va_list ap,
                                  const char *error) {
	fprintf(stream, "%s%s: ", date, name);
	vfprintf(stream, fmt, ap);
	va_end(ap);
	if (error) {
		fprintf(stream, ": %s\n", error);
	} else {
		putc('\n', stream);
	}
}



/****************************** Music Config ******************************/
int   music_config(struct module *m, const struct music_option *options,
                   const char *opt, const char *arg, int req) {
	const struct music_option *o = options;
	while (o->opt && strcmp(o->opt, opt)) ++o;
	if (!o->opt) {
		if (req) {
			music_log(m, LOG_FATAL, "config: %s: unknown option", opt);
			return -1;
		} else {
			return 0;
		}
	}

	if (!o->arg) {
		if (*arg) {
			music_log(m, LOG_FATAL, "config: %s: unexpected argument", opt);
			return -1;
		}
	} else {
		if (!*arg) {
			music_log(m, LOG_FATAL, "config: %s: argument expected", opt);
			return -1;
		}
		if (o->arg==2) {
			char *end;
			errno = 0;
			strtol(arg, &end, 0);
			if (*end || errno) {
				music_log(m, LOG_FATAL, "config: %s: %s: integer expected",
				          opt, arg);
				return -1;
			}
		}
	}

	return o->ret;
}



/****************************** String Duplicate ******************************/
char *music_strdup_realloc(char *old, const char *str) {
	size_t len = strlen(str) + 1;
	old = realloc(old, len);
	memcpy(old, str, len);
	return old;
}



/****************************** Poll ******************************/
int music_sleep(struct module *m, unsigned long mili) {
	int ret = 0;

#ifdef HAVE_POLL
	struct pollfd fd = { 0, POLLIN, 0 };
	if (!mili) return 0;

	fd.fd = sleep_pipe_fd;
	while (mili > INT_MAX && !(ret = poll(&fd, 1, mili))) {
		mili -= INT_MAX;
	}
	ret = ret ? ret : poll(&fd, 1, mili);
#else
	struct timeval timeout = {
		mili / 1000,
		(mili % 1000) * 1000
	}
	fd_set set;
	if (!mili) return 0;

	FD_ZERO(&set);
	FD_SET(sleep_pipe_fd, &set);
	ret = select(sleep_pipe_fd + 1, &set, 0, 0, &timeout);
#endif

	if (ret==-1) {
#ifdef HAVE_POLL
		music_log_errno(m, LOG_WARNING, "poll");
#else
		music_log_errno(m, LOG_WARNING, "select");
#endif
		return 1;
	} else {
		return !ret;
	}
}
