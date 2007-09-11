/*
 * "Listening to" daemon configuration file
 * $Id: config.h,v 1.4 2007/09/11 14:43:57 mina86 Exp $
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

#ifndef MN_CONFIG_H
#define MN_CONFIG_H


#define HAVE_POLL 1
#define HAVE_OPENSSL_H 0
#define HAVE_ENDIAN_H 1


#ifndef __GNUC__
# define __attribute__(x)
# if __STDC_VERSION__ + 0 >= 199901L
#  define __inline__ inline
#  define __restrict__ restrict
# else
#  define __inline__
#  define __restrict__
# endif
#endif


#endif
