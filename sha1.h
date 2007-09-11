/*
 * SHA1 Implementation
 * $Id: sha1.h,v 1.1 2007/09/11 14:49:13 mina86 Exp $
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
#include <stdint.h>

uint8_t *sha1    (uint8_t hash[20],
                  const uint8_t *__restrict__ message, uint32_t len);
char    *sha1_hex(char hash[41],
                  const uint8_t *__restrict__ message, uint32_t len);
char    *sha1_b64(char hash[29],
                  const uint8_t *__restrict__ message, uint32_t len);

#endif
