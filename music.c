/*
 * "Listening to" daemon
 * $Id: music.c,v 1.7 2007/09/19 14:00:14 mina86 Exp $
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

#include "music-int.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>



/**
 * A conf method for core module.  See music_module::conf.
 *
 * @param m core module.
 * @param opt option keyword.
 * @param arg argument.
 * @return whether option was accepted.
 */
static int  config_line(const struct music_module *m,
                        const char *opt, const char *arg)
	__attribute__((nonnull(1)));


/**
 * Parses signle configuration line.  It executes configuration's conf
 * method (music_module::conf) or loads module if option was "module".
 * It stores which module configuration it's reading now in *m_.  When
 * executed for the first time (or when a new configuration file is
 * being read ) m_ should be initialised to point to pointer to core
 * module.
 *
 * @param buf line from configuration file.
 * @param m_ pointer to module being configured now.
 * @return zero on error, non-zero on success.
 */
static int  parse_line(char *buf, struct music_module **m_)
	__attribute__((nonnull));


/**
 * Sorts modules according to it's type in the following order: core,
 * cache, out, int.
 *
 * @param core core module.
 * @return zero if one of the modules had invalid type, non-zero on success.
 */
static int  sort_modules(struct music_module *core) __attribute__((nonnull));



volatile sig_atomic_t music_running = 1;
int sleep_pipe_fd;



/**
 * Signal number application recieved or 0 if none.
 */
static volatile sig_atomic_t sig = 0;

/**
 * A callback function for signals which should terminate application.
 * If sig is zero sets it to proper value otherwise calls abort.
 *
 * @param signum signal number.
 */
static void got_sig(int signum);

/**
 * A callback function for signals to ignore.  Does completly nothing.
 *
 * @param signum signal number.
 */
static void ignore_sig(int signum);



/****************************** Main ******************************/
int main(int argc, char **argv) {
	struct config cfg = {
		PTHREAD_MUTEX_INITIALIZER,
		{
			0,
			PTHREAD_MUTEX_INITIALIZER,
			PTHREAD_COND_INITIALIZER,
			0
		},
		0, LOG_NOTICE, 0,
		0
	};
	struct music_module core = {
		-1,
		0, 0, 0,
		config_line,
		{ 0 }, 0,
		0, 0,
		(char*)"core",
		0
	}, *m;
	char *ch;
	int i, returnValue = 0, pipe_fds[2];

	core.core = &core;
	core.data = &cfg;


	/***** Get program name *****/
	for (core.name = ch = *argv; *ch; ++ch) {
		if ((*ch=='/' || *ch=='\\') && ch[1] && ch[1]!='/' && ch[1]!='\\') {
			core.name = ch + 1;
		}
	}


	/***** Help *****/
	if (argc>=2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		fputs("usage: music [ config-file ... ]\n", stdout);
		return 0;
	}


	/***** Read config *****/
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
			int result = !parse_line(buf, &m);
			if (result) return result;
		}

		if (fp!=stdin) {
			fclose(fp);
		}
	}

	if (!sort_modules(&core)) {
		return 1;
	}



	/***** Open log file *****/
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


	/***** Daemonize *****/
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


	/***** Create sleep pipe *****/
	if (pipe(pipe_fds)) {
		music_log_errno(&core, LOG_FATAL, "pipe");
		return 1;
	}
	sleep_pipe_fd = pipe_fds[0];


	/***** Register signal handler *****/
	signal(SIGHUP,  got_sig);
	signal(SIGINT,  got_sig);
	signal(SIGILL,  got_sig);
	signal(SIGQUIT, got_sig);
	signal(SIGSEGV, got_sig);
	signal(SIGTERM, got_sig);
	signal(SIGALRM, ignore_sig);


	/***** Start cache *****/
	m = &core;
	while (m->next && m->next->type==MUSIC_CACHE) {
		struct music_module *prev = m;
		m = m->next;

		if (sig) {
			goto finishSig;
		}

		music_log(m, LOG_NOTICE, "starting");
		if (!m->start || m->start(m)) {
			music_log(m, LOG_DEBUG, "this will be our cache");
			break;
		}

		music_log(m, LOG_FATAL + 2, "error starting module");
		prev->next = m->next;
		if (m->free) m->free(m);
		free(m);
		m = prev;
	}

	/* Check cache */
	if (m==&core && cfg.requireCache) {
		music_log(m, LOG_FATAL, "no cache started");
		returnValue = 1;
		goto finishNoStop;
	}

	/* Free rest of the caches */
	while (m->next && m->next->type==MUSIC_CACHE) {
		struct music_module *next = m->next;
		m->next = next->next;
		if (next->free) next->free(next);
		if (next->name) free(next->name);
		free(next);
	}


	/***** Start other modules *****/
	while (m->next) {
		struct music_module *prev = m;
		m = m->next;

		if (sig) {
			goto finishSig;
		}

		music_log(m, LOG_NOTICE, "starting");
		if (m->start && !m->start(m)) {
			music_log(m, LOG_FATAL, "error starting module");
			prev->next = 0;
			returnValue = 1;
			goto finishNoSig;
		}
	}


	/***** Run *****/
	while (music_running) {
		pause();
	}


	/***** Check signal *****/
	if (sig) {
	finishSig:
		music_log(&core, LOG_NOTICE + 2, "got signal %d; exiting", sig);
	}

 finishNoSig:
	/* Stop everything */
	write(pipe_fds[1], "B", 1);
	for (m = core.next; m; m = m->next) {
		music_log(m, LOG_NOTICE + 2, "stopping");
		if (m->stop) m->stop(m);
	}

 finishNoStop:
	/* OS will free all resources we were using so no need to do it
	   ourselfves */
	music_log(&core, LOG_NOTICE, "terminated");
	return returnValue;
}



/****************************** Parse Line ******************************/
static int  parse_line(char *buf, struct music_module **m_) {
	struct music_module *m    = *m_;
	struct music_module *core = m->core;
	struct music_module *(*init)(const char *name, const char *arg);
	char *option, *moduleName, *argument, *ch, *end;
	void *handle;
	size_t len;


	/* Split line into option and arguments */
	for (option  = buf;         isspace(*option)         ; ++option);
	for (ch  = option ; *ch && !isspace(*ch ) && *ch!='#'; ++ch);
	for (end = ch     ;         isspace(*ch )            ; ++ch);
	*end = 0;
	if (!*option) return 1;
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
			return 0;
		} else if (!*argument) {
			music_log(m, LOG_FATAL, "name: argument expected");
			return 0;
		}
		m->name = realloc(m->name, len + 1);
		memcpy(m->name, argument, len + 1);
		return 1;
	}


	/* Pass arguments to module */
	if (strcmp(option, "module")) {
		if (m->config) {
			return m->config(m, option, argument);
		} else {
			music_log(m, LOG_FATAL, "%s: unknown option", option);
			return 0;
		}
	}


	/* Configuration for current module has ended */
	if (m->config && !m->config(m, 0, 0)) {
		return 0;
	}


	/* Module to load not specified */
	if (!*argument) {
		music_log(core, LOG_FATAL, "module: argument expected");
		return 0;
	}


	/* Split module name and argument */
	for (ch = moduleName = argument; *ch && !isspace(*ch); ++ch);
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
		return 0;
	}

	/* Load "init" function */
	dlerror();
	if (!(handle = dlsym(handle, "init"))) {
		music_log(core, LOG_FATAL, "%s", dlerror());
		free(moduleName);
		return 0;
	}

	/* Run "init" function */
	*(void **)&init = handle;
	m = init(moduleName, argument);
	if (!m) {
		music_log(core, LOG_FATAL, "%s: init: unknown error", buf);
		free(moduleName);
		return 0;
	}

	/* Fill structure */
	if (m->name) free(m->name);
	m->name = moduleName;
	m->next = core->next;
	m->core = core;
	core->next = m;
	*m_ = m;
	return 1;
}



/****************************** Sort modules ******************************/
static int  sort_modules(struct music_module *core) {
	struct music_module *buckets[3] = { 0, 0, 0 }, *last[3] = { 0, 0, 0 }, *m;
	unsigned i;

	/* Split into buckets */
	m = core->next;
	while (m) {

		struct music_module *next = m->next;
		unsigned type = (unsigned)m->type;

		if (type>2) {
			music_log(m, LOG_FATAL, "invalid module type: %d", (int)type);
			return 0;
		}

		m->next = buckets[type];
		buckets[type] = m;
		if (!last[type]) {
			last[type] = m;
		}

		m = next;
	}

	/* Connect buckets */
	m = core;
	for (i = 3; i; ) {
		if (buckets[--i]) {
			m->next = buckets[i];
			m = last[i];
		}
	}

	return 1;
}



/****************************** Config Line ******************************/
static int  config_line(const struct music_module *m,
                        const char *opt, const char *arg) {
	static struct music_option options[] = {
		{ "logfile" , 1, 1 },
		{ "loglevel", 2, 2 },
		{ "requirecache", 0, 3 },
		{ 0, 0, 0 }
	};
	struct config *const cfg = m->data;

	if (!opt) return 1;

	switch (music_config(m, options, opt, arg, 1)) {
	case -1:
	case  0: return 0;
	case  1: {
		size_t len = strlen(arg) + 1;
		cfg->logfile = realloc(cfg->logfile, len);
		memcpy(cfg->logfile, arg, len);
		break;
	}
	case  2:
		cfg->loglevel = atoi(arg);
		break;
	case 3:
		cfg->requireCache = 1;
		break;
	}
	return 1;
}



/****************************** Signals ******************************/
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
