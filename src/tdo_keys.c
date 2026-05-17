/*
  ISC License

  Copyright (c) 2025, Antonio SJ Musumeci <trapexit@spawn.link>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#define HAVE_C99INCLUDES

#include "bigd.h"
#include "md5.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static const char M1_RETAIL_3DO_N_STR[] = "B19462B00D8D6E1EC909AB385E06FE034BFD282E9FFDC584838C15F12593DD1E3A8B5626F1B9D0ED0C384EF6C5D14512BD72DDB85B44080E0472C03D0AFC4C97";
static const char M1_RETAIL_3DO_D_STR[] = "42F7CD9BCD109805BE150A60107D9C8F8BB9A5CCA78361588EEF665AF1ABE887DBC2593D0868F364A93C8CB8CC6F4BCC6A3DE57E04B17AC52F2649939C453F61";
static const char M1_RETAIL_3DO_P_STR[] = "E1BE29DE79315A1CD384C61DB3BACA5227C2D5A2020899283328C8E9C9B53BB1";
static const char M1_RETAIL_3DO_Q_STR[] = "C9619D5BBAEB0DEBCC6D144E380C987108C33FA379BB0CE1F714A7E92DF0C6C7";
static const char M1_RETAIL_3DO_E_STR[] = "10001";

static const char M1_RETAIL_APP_N_STR[] = "BC0B199086C7F26CBC9D50F404944DB4789FCBFCF7AD8DBC2120898ABEAAF311EEA20229035608841FA41073ABBD5D37500C60B53BFB46605740381B72C9DB71";
static const char M1_RETAIL_APP_D_STR[] = "18B2207E61A51ACA7B0EF215CA102C105A9329F824130FFD38208CCFC2F0B2915B8AD1E7772334381737D232B183C869C34940BC8769C97E18D7B0E78C492991";
static const char M1_RETAIL_APP_P_STR[] = "CBBA701095E52D2D96F4153328F7B85D147D273D1033AE034721F1B09A96FED5";
static const char M1_RETAIL_APP_Q_STR[] = "EC4A6C856F69EA7F910C4327E4586DCFAEC8C6E7AC875A435AD6EDB7476AD02D";
static const char M1_RETAIL_APP_E_STR[] = "10001";

static const char M1_RETAIL_MSG_PREFIX_STR[] = "1ffffffffffffffffffffffffffffffffffffffffffffffffffffff003020300c06082a864886f70d020505000410";

bool
streq(const char *s0_,
      const char *s1_)
{
  return (strcmp(s0_,s1_) == 0);
}

static
BIGD
bigd_from_hex_str(const char *s_)
{
  BIGD bigd;

  bigd = bdNew();

  bdConvFromHex(bigd,s_);

  return bigd;
}

BIGD
tdo_keys_m1_retail_3do_n(void)
{
  return bigd_from_hex_str(M1_RETAIL_3DO_N_STR);
}

BIGD
tdo_keys_m1_retail_3do_d(void)
{
  return bigd_from_hex_str(M1_RETAIL_3DO_D_STR);
}

BIGD
tdo_keys_m1_retail_app_n(void)
{
  return bigd_from_hex_str(M1_RETAIL_APP_N_STR);
}

BIGD
tdo_keys_m1_retail_app_d(void)
{
  return bigd_from_hex_str(M1_RETAIL_APP_D_STR);
}

BIGD
tdo_keys_m1_retail_message(md5_digest_t digest_)
{
  char str[128];

  snprintf(str,
           sizeof(str),
           "%s%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
           M1_RETAIL_MSG_PREFIX_STR,
           digest_[0x0],digest_[0x1],digest_[0x2],digest_[0x3],
           digest_[0x4],digest_[0x5],digest_[0x6],digest_[0x7],
           digest_[0x8],digest_[0x9],digest_[0xA],digest_[0xB],
           digest_[0xC],digest_[0xD],digest_[0xE],digest_[0xF]);

  return bigd_from_hex_str(str);
}

BIGD
tdo_keys_n(const char *key_)
{
  if(streq(key_,"3do"))
    return tdo_keys_m1_retail_3do_n();
  if(streq(key_,"app"))
    return tdo_keys_m1_retail_app_n();
  assert(false);
}

BIGD
tdo_keys_d(const char *key_)
{
  if(streq(key_,"3do"))
    return tdo_keys_m1_retail_3do_d();
  if(streq(key_,"app"))
    return tdo_keys_m1_retail_app_d();
  assert(false);
}

BIGD
tdo_keys_m(const char   *key_,
           md5_digest_t  digest_)
{
  if(streq(key_,"3do"))
    return tdo_keys_m1_retail_message(digest_);
  if(streq(key_,"app"))
    return tdo_keys_m1_retail_message(digest_);
  assert(false);
}
