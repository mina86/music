/*
 * "Listening to" daemon internal header file
 * $Id: music-int.h,v 1.1 2007/09/19 02:32:57 mina86 Exp $
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

#ifndef MUSIC_INTERNAL_H
#define MUSIC_INTERNAL_H


#define MUSIC_NOT_MODULE
#include "music.h"


/**
 * A linked list element.
 */
struct slist {
	struct slist *next; /**< Next element. */
	void *ptr;          /**< Data pointer. */
};



/**
 * A core module configuration structure.
 */
struct config {
	/**
	 * Mutex used by log function. This way only one message is
	 * raported at a time thus there are no problems with messages
	 * starting in the middle of another message.
	 */
	pthread_mutex_t log_mutex;

	/**
	 * Configuration for songs disptcher.
	 */
	struct music_config_songs {
		pthread_t dispatcher;   /**< Song dispatcher's thread ID/ */
		pthread_mutex_t mutex;  /**< Mutex used by song dispatcher. */
		pthread_cond_t cond;    /**< Song dispatcher's condition variable. */
		struct slist *first;    /**< First song on songs queue. */
	} songs;

	char    *logfile;           /**< A log file. */
	unsigned loglevel;          /**< A log level.  Messages with
                                   greater level (less important)
                                   won't be logged. */
	unsigned logboth;           /**< A temporary internal variable. */
	unsigned requireCache;      /**< Whether user specified that cache
                                   is required module. */
};


#endif
