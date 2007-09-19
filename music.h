/*
 * "Listening to" daemon header file
 * $Id: music.h,v 1.8 2007/09/19 14:00:14 mina86 Exp $
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

#ifndef MUSIC_H
#define MUSIC_H

#include "config.h"

#include <pthread.h>
#include <signal.h>



/**
 * Structure representing a song.
 */
struct music_song {
	const char *title;   /**< Song's title. */
	const char *artist;  /**< Song's performer. */
	const char *album;   /**< Song's album. */
	const char *genre;   /**< Song's genre. */
	time_t time;         /**< The time song was reported. */
	time_t endTime;      /**< The time song (will) end(ed). */
	unsigned length;     /**< Song's length in seconds. */
};



/**
 * Structure used with music_config() function.
 */
struct music_option {
	const char *opt;  /** Option keyword to recognise. */
	/**
	 * What kind of argument option takes.
	 */
	enum {
		MUSIC_OPT_NO_ARG  = 0,  /**< No argument. */
		MUSIC_OPT_STRING  = 1,  /**< A (nonempty) string argument. */
		MUSIC_OPT_NUMERIC = 2   /**< A valid integer. */
	} arg;
	int ret;          /**< Value music_config() will return when this
	                       option is encontered. */
};



/**
 * Structure representing single module.  Unless specified otherwise
 * all fields must be initialised by module that is either by by
 * init() function (each module must provide) or config function (if
 * such function was set by init().  Also values of such fields may be
 * changed in config function.
 */
struct music_module {
	/**
	 * Module type.
	 */
	enum {
		MUSIC_CORE   = -1,  /**< Reserved for core. */
		MUSIC_IN     =  0,  /**< Module is an input module. */
		MUSIC_OUT    =  1,  /**< Module is an output module. */
		MUSIC_CACHE  =  2   /**< Module is a cache module. */
	} type;


	/**
	 * Method executed once when module is started.  Must return
	 * non-zero on success and zero on failure.  If starting failed
	 * stop() won't be called.  All modules must implement this.
	 *
	 * @param m module.
	 * @return non-zero on success, zero on failure.
	 */
	int  (*start)(const struct music_module *m);


	/**
	 * Method executed once when module is being stopped.
	 *
	 * @param m module.
	 */
	void (*stop)(const struct music_module *m);


	/**
	 * Method executed once to free memory.  It must free all memory used
	 * by module but not the module structure itself -- this will be
	 * done by core.  Required if module allocates aditional memory
	 *  during configuration.
	 *
	 * @param m module.
	 */
	void (*free)(struct music_module *m);


	/**
	 * Method executed for each configuration line plus once when
	 * config for particular module has ended executed with opt and
	 * arg NULL.
	 *
	 * Note that it must be initialised by init() if one intents to
	 * use it as only init() and this method can modify module
	 * structure.
	 *
	 * @param m module.
	 * @param opt option or NULL when configuration ended.
	 * @param arg argument or NULL when configuration ended.
	 * @return non-zero on success, zero on error.
	 */
	int  (*config)(const struct music_module *m, const char *opt,
	                const char *arg);


	/**
	 * Methods handling new songs.  They are mutually exlusive either
	 * for output or cache modules .
	 */
	union music_module_song_method {
		/**
		 * Submitts songs.  Module must try to submit all songs and
		 * return number of songs which it *failed* to submit.
		 * Moreover, errorPositions array must be filled with indexes
		 * of songs which failed to be submitted.  Or, if module
		 * failed to submit all songs it may return -1 and leave
		 * errorPositions intact.  This method is required for output
		 * modules.
		 *
		 * Note that if module submits songs somewhere where they may
		 * be rejected it is most likely irrelevant if such song was
		 * rejected or not.  Even if it was rejected module must not
		 * raport is as failure since resubmitting the song won't make
		 * it magically be accepted.
		 *
		 * @param m output module.
		 * @param songs a NULL terminated array of pointers to songs.
		 * @param errorPositions array to fill with indexes of songs
		 *                       that module was unable to submit.
		 * @return number of songs method failed to submit or -1.
		 */
		int (*send)(const struct music_module *m,
		            const struct music_song *const *songs,
		            size_t *errorPositions);

		/**
		 * Stores song in cache for later resubmission.  Module must
		 * store it and associate it with names of given modules.
		 * This way, songs may be later resubmitted if given module'll
		 * become operational again.
		 *
		 * @param m cache module.
		 * @param song song to save.
		 * @param modules a NULL terminated array of pointers to
		 *                output modules.
		 */
		void (*cache)(const struct music_module *m, const struct music_song *song,
		              const struct music_module *const *modules);
	} song;


	/**
	 * Method called once in a while to request cache to try to resend
	 * cached songs.  Parameteres are a list of out modules to send
	 * to.  It is cache module responsibility to call song.send
	 * functions and interprete their result.  This is required for
	 * cache modules.
	 *
	 * @param m cache module.
	 * @param modules a NULL terminated array of pointers to output
	 *        modules.
	 */
	void (*retryCached)(const struct music_module *m,
	                    const struct music_module *const *modules);



	/* Those two are for internal use by core.  init() (or module in
	   any other place) should not touch them. */
#ifdef MUSIC_INTERNAL_H
	struct music_module *next;  /**< Next module.  For internal use! */
	struct music_module *core;  /**< The core module.  For internal use! */
#else
	/** Internal data for use by core.  Modules must not touch it! */
	char internal[sizeof(struct music_module*[2])];
#endif


	/**
	 * Module name (not neccesary file name).  It may be set by user
	 * using name configuration option.  Module must not touch it.
	 */
#ifdef MUSIC_INTERNAL_H
	char *name;
#else
	const char *const name;
#endif

	/**
	 * Data pointer for use by module.  Core will never touch this.
	 */
	void *data;
};



/**
 * Specifies whether application is still running or is it about to
 * finish.  Modules should check value of this variable and finish
 * executing when it is zero.
 */
#ifdef MUSIC_INTERNAL_H
extern volatile sig_atomic_t music_running;
#else
extern const volatile sig_atomic_t music_running;
#endif


/**
 * Specifies a file description for a pipe on which core sends dummy
 * data when finishing.  Modules should use this pipe as an argument
 * to select() or poll() when they are sleeping.  Also when they are
 * select()ing or poll()ing they should include this file descriptor
 * and finish execution when any data were written into it.
 *
 * Modules <strong>MUST NOT</strong> read any data from this file
 * descriptor.
 */
#ifdef MUSIC_INTERNAL_H
extern int sleep_pipe_fd;
#else
extern const int sleep_pipe_fd;
#endif



/**
 * Specifies various log levels.  Notice that there are levels between
 * those specified.  For instance 8 -- one may thing of it as a "wery
 * important warning" or "rather not important error". ;)
 */
enum {
	LOG_FATAL   =  0,  /**< Fatal errors which causes application to
                        *   terminate.  Should be raported only by
                        *   config and start methods. */
	LOG_ERROR   =  4,  /**< Errors. */
	LOG_WARNING =  8,  /**< Warnings. */
	LOG_NOTICE  = 12,  /**< Notices. */
	LOG_DEBUG   = 16   /**< Debug messages. */
};



/**
 * Formatts message and send it to log file.  Format must not contain
 * new line character at the end (or anywhere else for that matter).
 *
 * @param m module raporting message.
 * @param level message's level.
 * @param fmt message format (same as in printf()).
 */
void  music_log   (const struct music_module *m, unsigned level,
                   const char *fmt, ...)
	__attribute__((format (printf, 3, 4), nonnull, visibility("default")));



/**
 * Formatts message and send it to log file.  This function adds
 * a colon followed by space followed by an error message returned by
 * strerror() after the message.  It is here because strerror() is not
 * thread-safe function and using this function is easier then plaing
 * with strerror_r().
 *
 * @param m module raporting message.
 * @param level message's level.
 * @param fmt message format (same as in printf()).
 */
void  music_log_errno(const struct music_module *m, unsigned level,
                      const char *fmt, ...)
	__attribute__((format (printf, 3, 4), nonnull, visibility("default")));



/**
 * Searches for an option in specified options list and then validates
 * its argument.  If option was not found then if req is zero function
 * will simply return 0 otherwise it will a fatal error and return -1.
 * If option was found then if argument is valid function will return
 * value specified for given option otherwise it will log a fatal
 * error and return -1.
 *
 * @param m module.
 * @param options a NULL terminated list of options.
 * @param opt option keyword.
 * @param arg argument.
 * @param req whether it must be found.
 * @return -1 on error, 0 if option was not found or option associated
 *         with an option.
 */
int   music_config(const struct music_module *m,
                   const struct music_option *options,
                   const char *opt, const char *arg, int req)
	__attribute__((nonnull, visibility("default")));



/**
 * Puts given song on song queue so that song dispatcher can then
 * instruct output plugins to submit this song.
 *
 * @param m input module that raports song.
 * @param song a song it raports.
 */
void  music_song  (const struct music_module *m,
                   const struct music_song *song)
	__attribute__((nonnull, visibility("default")));



/**
 * Allocates memory and duplicates given string.  This function uses
 * realloc() on ginve old pointer which can be NULL.  The returned
 * value may be equal to old but doesn't have to.
 *
 * @param old a pointer to allocated memory or NULL.
 * @param str string to duplicate.
 * @return duplicated string.
 */
char *music_strdup_realloc(char *old, const char *str)
	__attribute__((nonnull(2), visibility("default")));



/**
 * Allocates memory and duplicates given string.
 *
 * @param x string to duplicate.
 * @return duplicated string.
 */
#define music_strdup(x) music_strdup_realloc(0, x)



/**
 * Sleeps at least given number miliseconds.  This function does
 * select()ing or poll()ing on a sleep_pipe_fd and therefore will be
 * interrupted when core decides to terminate.  If so happens it will
 * return 1. It will also return -1 on error.  Otherwise it will return
 * 0.
 *
 * @param m module that wants to sleep.
 * @param mili number of miliseconds it wants to sleep.
 * @return -1 on error, 1 when core begins terminating, 0 otherwise.
 */
int music_sleep(const struct music_module *m, unsigned long mili)
	__attribute__((nonnull, visibility("default")));



/**
 * Instructs cache that given module is ready to send songs if there
 * are any pending to send for that module.  This may called by output
 * module after it recovered from a situation that made it impossible
 * for it to submit songs.
 *
 * @param m output module that is ready to submit songs.
 */
void music_retry_cached(const struct music_module *m)
	__attribute__((nonnull, visibility("default")));



/**
 * Checks if it's the first time it was called with given arguments.
 * This should be used by modules which run functions from external
 * libraries which should not be run more then once (ie. some
 * initialisation functions).  For instance:
 *
 * \code
 * if (music_run_once_check((void(*)(void))library_xyz_init_function, 0)) {
 *     library_xyz_init_function();
 *     atexit(library_xyz_done_function);
 * }
 * \endcode
 *
 * It may be also used to register functions to be run at exit once, ie.:
 *
 * \code
 * if (music_run_once_check((void(*)(void))atexit, (void*)library_xyz_done)) {
 *     atexit(library_xyz_done);
 * }
 * \endcode
 *
 * This function is thread unsafe!
 *
 * @param func a function pointer.
 * @param arg argument.
 * @returns 1 if it is the first time function was called with given
 *          arguments.
 */
int  music_run_once_check(void (*func)(void), void *arg)
	__attribute__((nonnull(1), visibility("default")));



#ifndef MUSIC_INTERNAL_H
/**
 * Initialises module.  This function must be exported by all modules.
 * It is run before any threads are started and it's the only place
 * module can run thread unsafe code.
 *
 * @param name module file name (or sort of).
 * @param arg argument given in configuration "module" directive after
 *            module name.
 * @return an initialised music_module structure or NULL on error.
 */
struct music_module *init(const char *name, const char *arg)
	__attribute__((nonnull, visibility("default")));
#endif


#endif
