/*
 * "Listening to" daemon configuration file
 * $Id: config.h,v 1.7 2007/09/26 17:53:21 mina86 Exp $
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

#include <stdint.h>


#define HAVE_POLL      1  /**< Defined as 1 when we have a poll() function. */
#define HAVE_OPENSSL_H 0  /**< Defined as 1 when we have OpenSSL. */
#define HAVE_ENDIAN_H  1  /**< Defined as 1 when we have endian.h. */


#if __STDC_VERSION__ < 199901L
#  if defined __GNUC__
#    define inline   __inline__
#    define restrict __restrict__
#  else
#    define inline   /**< Defined for compilers not supporting inline. */
#    define restrict /**< Defined for compilers not supporting restrict. */
#  endif
#endif

#if !defined __GNUC__ && !defined __attribute__
#  define __attribute__(x) /**< Defined for compilers different then GCC. */
#endif


#endif
