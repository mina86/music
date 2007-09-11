/*
 * SHA1 Implementation
 * $Id: sha1.c,v 1.1 2007/09/11 14:49:13 mina86 Exp $
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

#include "sha1.h"
#include <stdio.h>
#include <string.h>


char    *sha1_hex(char hash[41],
                  const uint8_t *__restrict__ message, uint32_t len) {
	static const char hexdigits[16] = "0123456789abcdef";
	uint8_t temp[20], *rd = temp, *const end = temp + 20;
	char *wr = hash;

	sha1(rd, message, len);

	while (rd!=end) {
		*wr++ = hexdigits[*rd >> 4];
		*wr++ = hexdigits[*rd & 15];
		++rd;
	}

	*wr = 0;
	return hash;
}



char    *sha1_b64(char hash[29],
                  const uint8_t *__restrict__ message, uint32_t len) {
	static const char b64digits[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	uint8_t temp[20], *rd = temp, *const end = temp + 18;
	char *wr = hash;

	sha1(rd, message, len);

	while (rd!=end) {
		*wr++ = b64digits[rd[0] >> 2];
		*wr++ = b64digits[((rd[0] & 3) << 4) | (rd[1] >> 4)];
		*wr++ = b64digits[((rd[1] & 15) << 2) | (rd[2] >> 6)];
		*wr++ = b64digits[rd[2] & 63];
		rd += 3;
	}

	*wr++ = b64digits[rd[0] >> 2];
	*wr++ = b64digits[((rd[0] & 3) << 4) | (rd[1] >> 4)];
	*wr++ = b64digits[(rd[1] & 15) << 2];
	*wr++ = '=';
	*wr = 0;
	return hash;
}



/*************** uint32_t <-> big endian conversion macros ***************/
#if HAVE_ENDIAN_H
#  include <endian.h>
#  if __BYTE_ORDER == __BIG_ENDIAN
#    define SHA1_BIG_ENDIAN 1
#  else
#    define SHA1_BIG_ENDIAN 0
#  endif
#else
#  define SHA1_BIG_ENDIAN 0
#endif

#if SHA1_BIG_ENDIAN
#  define INT2BE(dest, val) (*((uint32_t*)(dest)) = (val))
#  define BE2INT(dest) (*((uint32_t*)(dest)))
#else
#  define INT2BE(dest, val) do {				\
		(dest)[0] = (val) >> 24;				\
		(dest)[1] = (val) >> 16;				\
		(dest)[2] = (val) >>  8;				\
		(dest)[3] = (val)      ;				\
	} while (0)
#  define BE2INT(dest)							\
	(((uint32_t)((dest)[0])) << 24) |			\
	(((uint32_t)((dest)[1])) << 16) |			\
	(((uint32_t)((dest)[2])) <<  8) |			\
	(((uint32_t)((dest)[3]))      )
#endif



/******************** Use OpenSSL if we have it ********************/
#if HAVE_OPENSSL_H
#include <openssl/sha.h>

uint8_t *sha1    (uint8_t hash[20],
                  const uint8_t *__restrict__ message, uint32_t len) {
	SHA1((const unsigned char *)message, (unsigned long)len,
	     (unsigned char*)hash);
	return hash;
}



/******************** Otherwise our own implementation ********************/
#else
/***** Rotates left value by n bits (0<n<32) *****/
static __inline__ uint32_t rol(uint32_t value, unsigned n) {
	return (value << n) | (value >> (32 - n));
}



/***** Handles single 512-bit long (64-byte long) block *****/
static void sha1_block(const uint8_t block[64], uint32_t state[5]) {
	uint32_t w[16], a = state[0], b = state[1], c = state[2], d = state[3],
		e = state[4];
	unsigned i;
	const uint8_t *bl = block;

	/*  0 <= i < 16 */
	for (i = 0; i < 16; ++i) {
		uint32_t f = 0x5A827999 + (d ^ (b & (c ^ d)));
		w[i] = BE2INT(bl);
		bl += 4;
		f += rol(a, 5) + e + w[i];
		e = d; d = c; c = rol(b, 30); b = a; a = f;
	}

	/* 16 <= i < 20 */
	for (; i < 20; ++i) {
		uint32_t f = 0x5A827999 + (d ^ (b & (c ^ d)));
		unsigned j = i & 15;
		w[j] = rol(w[(j+13)&15] ^ w[(j+8)&15] ^ w[(j+2)&15] ^ w[j], 1);
		f += rol(a, 5) + e + w[j];
		e = d; d = c; c = rol(b, 30); b = a; a = f;
	}

	/* 20 <= i < 40 */
	for (; i < 40; ++i) {
		uint32_t f = 0x6ED9EBA1 + (b ^ c ^ d);
		unsigned j = i & 15;
		w[j] = rol(w[(j+13)&15] ^ w[(j+8)&15] ^ w[(j+2)&15] ^ w[j], 1);
		f += rol(a, 5) + e + w[j];
		e = d; d = c; c = rol(b, 30); b = a; a = f;
	}

	/* 40 <= i < 60 */
	for (; i < 60; ++i) {
		uint32_t f = 0x8F1BBCDC + ((b & c) | (d & (b | c)));
		unsigned j = i & 15;
		w[j] = rol(w[(j+13)&15] ^ w[(j+8)&15] ^ w[(j+2)&15] ^ w[j], 1);
		f += rol(a, 5) + e + w[j];
		e = d; d = c; c = rol(b, 30); b = a; a = f;
	}

	/* 60 <= i < 80 */
	for (; i < 80; ++i) {
		uint32_t f = 0xCA62C1D6 + (b ^ c ^ d);
		unsigned j = i & 15;
		w[j] = rol(w[(j+13)&15] ^ w[(j+8)&15] ^ w[(j+2)&15] ^ w[j], 1);
		f += rol(a, 5) + e + w[j];
		e = d; d = c; c = rol(b, 30); b = a; a = f;
	}

	state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}



uint8_t *sha1    (uint8_t hash[20],
                  const uint8_t *__restrict__ message, uint32_t len) {
	uint32_t state[5] = {
		0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0
	}, i;
	uint8_t block[64];

	for (i = len; i>=64; i -= 64) {
		sha1_block(message, state);
		message += 64;
	}

	memcpy(block, message, i);
	block[i] = 0x80;
	memset(block+i+1, 0, 63 - i);

	if (i+9>64) {
		sha1_block(block, state);
		memset(block, 0, i+1);
	}

	INT2BE(block+56, len>>29);
	INT2BE(block+60, len<<3);
	sha1_block(block, state);

	for (i = 0; i < 5; ++i) {
		INT2BE(hash + (i<<2), state[i]);
	}
	return hash;
}
#endif



/******************** Tests ********************/
#ifdef SHA1_COMPILE_TEST
#include <stdlib.h>


int main(void) {
	static const struct test {
		const char *const text;
		const unsigned repeat;
		const char result[40];
	} tests[] = {
		{ "a", 1000000,
		  "34aa973cd4c4daa4f61eeb2bdbad27316534016f" },
		{ "abc", 1,
		  "a9993e364706816aba3e25717850c26c9cd0d89d" },
		{ "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1,
		  "84983e441c3bd26ebaae4aa1f95129e5e54670f1" },
		{ "0123456701234567012345670123456701234567012345670123456701234567", 10,
		  "dea356a2cddd90c7a7ecedc5ebb563934f460452" },

		{ "The quick brown fox jumps over the lazy dog", 1,
		  "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12" },
		{ "The quick brown fox jumps over the lazy cog", 1,
		  "de9f2c7fd25e1b3afad3e85a0bd17d9b100db4b3" },
		{ "", 1,
		  "da39a3ee5e6b4b0d3255bfef95601890afd80709" },

		{ 0, 0, "" }
	}, *test = tests;

	uint8_t *buffer = 0;
	char hash[41];
	size_t capacity = 0;
	int result = EXIT_SUCCESS, cmp;

	for (test = tests; test->text; ++test) {
		const size_t slen = strlen(test->text), rep = test->repeat;
		const size_t len = slen*rep;
		size_t i = 0;

		if (capacity < len) {
			uint8_t *tmp = realloc(buffer, len);
			if (!tmp) {
				printf("%02d: ERR not enough memory\n", test - tests);
				result = EXIT_FAILURE;
				continue;
			}
			buffer = tmp;
			capacity = len;
		}

		for (; i < rep; ++i) {
			memcpy(buffer + (i*slen), test->text, slen);
		}

		sha1_hex(hash, buffer, len);
		cmp = memcmp(hash, test->result, 40);
		printf("%02d: %s %s ", test - tests, cmp ? "ERR" : "OK ", hash);
		sha1_b64(hash, buffer, len);
		puts(hash);
		if (cmp) {
			result = EXIT_FAILURE;
		}
	}

	return result;
}
#endif
