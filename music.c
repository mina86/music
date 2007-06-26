#include <ctype.h>
#include <errno.h>
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
#include <signal.h>

#define MUSIC_NO_MODULE
#include "music.h"


static int  config_line(struct module *m, const char *opt, const char *arg)
	__attribute__((nonnull));
static int  parse_line(char *buf, struct module **m_)
	__attribute__((nonnull));


static int  sig = 0;
static void got_sig(int signum);


struct config {
	char    *logfile;
	unsigned loglevel;
	unsigned logboth;
};



/****************************** Main ******************************/
int main(int argc, char **argv) {
	struct config cfg = { 0, LOG_NOTICE, 0 };
	struct module_functions functions = {
		0, 0, 0, config_line, 0
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
			music_log(&core, LOG_FATAL, "open: %s: %s", argv[i],
			          strerror(errno));
			return 1;
		}

		m = &core;
		if (ch) *ch = 0;
		while (fgets(buf, sizeof buf, fp)) {
			int result = parse_line(buf, &m);
			if (result) return result;
		}
	}


	/* Open log file */
	if (cfg.logfile && *cfg.logfile) {
		i = open(cfg.logfile, O_WRONLY | O_APPEND | O_CREAT, 0600);
		if (i==-1) {
			music_log(&core, LOG_FATAL, "open: %s: %s", cfg.logfile,
			          strerror(errno));
			return 1;
		}
		fflush(stderr);
		dup2(i, 2); /* stderr is nog logfile */
		close(i);
	}

	music_log(&core, LOG_NOTICE, "starting");
	cfg.logboth = 1;

	/* Daemonize */
	switch (fork()) {
	case -1:
		music_log(&core, LOG_FATAL, "fork: %s", strerror(errno));
		return 1;
	case  0: break;
	default: return 0;
	}

	setsid();

	switch (fork()) {
	case -1:
		music_log(&core, LOG_FATAL, "fork: %s", strerror(errno));
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


	/* Register signal handler */
	signal(SIGHUP,  got_sig);
	signal(SIGINT,  got_sig);
	signal(SIGILL,  got_sig);
	signal(SIGQUIT, got_sig);
	signal(SIGSEGV, got_sig);
	signal(SIGTERM, got_sig);


	/* Start everything */
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
	if (!sig) pause();
	music_log(&core, LOG_NOTICE + 2, "got signal %d; exiting", sig);

	/* Stop everything */
	for (m = core.next; m; m = m->next) {
		music_log(m, LOG_NOTICE + 2, "stopping");
		if (m->f->stop) m->f->stop(m);
	}

	music_log(&core, LOG_NOTICE, "terminated");
	return 0;
}



/****************************** Parse Line ******************************/
static int  parse_line(char *buf, struct module **m_) {
	struct module *m    = *m_;
	struct module *core = m->core;
	struct module *(*init)();
	char *opt, *arg, *ch, *end;
	void *handle;
	size_t len;


	for (opt = buf;  isspace(*opt)            ; ++opt);
	for (ch  = opt; !isspace(*ch ) && *ch!='#'; ++ch );
	for (end = ch ;  isspace(*ch )            ; ++ch );
	*end = 0;
	if (!*opt) return 0;
	end = ch;
	for (arg = ch ;          *ch   && *ch!='#'; ++ch ) {
		if (!isspace(*ch)) end = ch + 1;
	}
	*end = 0;
	len = end - arg;


	if (!strcmp(opt, "name")) {
		if (m==core) {
			music_log(m, LOG_FATAL, "name: unknown option");
			return 1;
		}
		if (!*arg) {
			music_log(m, LOG_FATAL, "name: argument expected");
			return 1;
		}
		m->name = realloc(m->name, len + 1);
		memcpy(m->name, arg, len + 1);
		return 0;
	}


	if (strcmp(opt, "module")) {
		if (m->f->configLine) {
			return m->f->configLine(m, opt, arg);
		} else {
			music_log(m, LOG_FATAL, "%s: unknown option", opt);
			return 1;
		}
	}


	if (!*arg) {
		music_log(core, LOG_FATAL, "module: argument expected");
		return 1;
	}

	if (m->f->configEnd) {
		m->f->configEnd(m);
	}

	music_log(core, LOG_NOTICE, "%s: loading module", arg);

	buf[0] = '.'; buf[1] = '/';
	memmove(buf + 2, arg, len);
	memcpy(buf + 2 + len, ".so", 4);
	if (!(handle = dlopen(buf, RTLD_LAZY))) {
		music_log(core, LOG_FATAL, "%s", dlerror());
		return 1;
	}

	dlerror();
	if (!(handle = dlsym(handle, "init"))) {
		music_log(core, LOG_FATAL, "%s", dlerror());
	}

	*(void **)&init = handle;
	m = init();
	if (!m) {
		music_log(core, LOG_FATAL, "%s: init: unknown error", buf);
		return 1;
	}

	m->name = malloc(len + 1);
	memcpy(m->name, buf + 2, len);
	m->name[len] = 0;

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

	music_log(m, LOG_DEBUG, "got song: %s <%s> %s",
	          song->artist ? song->artist : "(null)",
	          song->album  ? song->album  : "(null)",
	          song->title  ? song->title  : "(null)");
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
	if (!sig) {
		sig = signum;
	} else {
		abort();
	}
}



/****************************** Music Log ******************************/
void music_log (struct module *m, unsigned level, const char *fmt, ...){
	static char buf[32];
	struct config *cfg = (struct config *)m->core->data;
	va_list ap;
	time_t t;

	if (cfg->loglevel < level) return;

	t = time(0);
	if (!strftime(buf, sizeof buf, "[%Y/%m/%d %H:%M:%S] ", gmtime(&t))) {
		buf[0] = 0;
	}

	fprintf(stderr, "%s%s: ", buf, m->name);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	putc('\n', stderr);

	if (!cfg->logboth) return;

	fprintf(stdout, "%s%s: ", buf, m->name);
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	putc('\n', stdout);
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
