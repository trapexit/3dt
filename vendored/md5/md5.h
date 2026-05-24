/*
 * This is an OpenSSL-compatible implementation of the RSA Data Security, Inc.
 * MD5 Message-Digest Algorithm (RFC 1321).
 *
 * Homepage:
 * http://openwall.info/wiki/people/solar/software/public-domain-source-code/md5
 *
 * Author:
 * Alexander Peslyak, better known as Solar Designer <solar at openwall.com>
 *
 * This software was written by Alexander Peslyak in 2001.  No copyright is
 * claimed, and the software is hereby placed in the public domain.
 * In case this attempt to disclaim copyright and place the software in the
 * public domain is deemed null and void, then the software is
 * Copyright (c) 2001 Alexander Peslyak and it is hereby released to the
 * general public under the following terms:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 *
 * See md5.c for more information.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

typedef uint8_t  md5_u8_t;
typedef uint32_t md5_u32_t;
typedef size_t   md5_size_t;
typedef uint8_t  md5_digest_t[16];

typedef struct md5_ctx_s md5_ctx_t;
struct md5_ctx_s
{
  md5_u32_t low;
  md5_u32_t high;
  md5_u32_t a;
  md5_u32_t b;
  md5_u32_t c;
  md5_u32_t d;
  md5_u32_t block[16];
  md5_u8_t  buffer[64];
};

#ifdef __cplusplus
extern "C"
{
#endif

extern void md5_init(md5_ctx_t *ctx);
extern void md5_update(md5_ctx_t *ctx, const void *data, md5_size_t size);
extern void md5_finalize(md5_ctx_t *ctx, md5_digest_t digest);
extern void md5_calc(const void *data, md5_size_t size, md5_digest_t digest);

#ifdef __cplusplus
}
#endif
