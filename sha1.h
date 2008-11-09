/**
 * SHA1 Implementation.
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

#ifndef MINA_SHA1_H
#define MINA_SHA1_H

#include "config.h"


/**
 * Calculates SHA1 of a given string.  Hash is saved in binary and
 * consists of 20 (8-bit) bytes.
 *
 * <var>hash</var> and <var>message</var> may overlap.
 *
 * @param hash location where to save hash.
 * @param message message to calculate SHA1 of.
 * @param len message's length in 8-bit bytes.
 * @return location where hash was saved (ie. value of hash argument).
 */
unsigned char *sha1(unsigned char hash[20], const unsigned char *message,
                    unsigned long len) __attribute__((nonnull));


/**
 * Calculates SHA1 of a given string.  Hash is saved in hex
 * representation and consists of 40 characters and a terminating NUL
 * byte.
 *
 * <var>hash</var> and <var>message</var> may overlap.
 *
 * @param hash location where to save hash.
 * @param message message to calculate SHA1 of.
 * @param len message's length in 8-bit bytes.
 * @return location where hash was saved (ie. value of hash argument).
 */
char    *sha1_hex(char hash[41], const unsigned char *message,
                  unsigned long len) __attribute__((nonnull));


/**
 * Calculates SHA1 of a given string.  Hash is saved in base64
 * encodind and consists of 28 characters (the last one is an equal
 * sign) and a terminating NUL byte.
 *
 * <var>hash</var> and <var>message</var> may overlap.
 *
 * @param hash location where to save hash.
 * @param message message to calculate SHA1 of.
 * @param len message's length in 8-bit bytes.
 * @return location where hash was saved (ie. value of hash argument).
 */
char    *sha1_b64(char hash[29], const unsigned char *message,
                  unsigned long len) __attribute__((nonnull));

#endif
