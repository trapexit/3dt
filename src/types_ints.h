#pragma once

#include <stdint.h>

#define S8_MIN  INT8_MIN
#define S8_MAX  INT8_MAX
#define S16_MIN INT16_MIN
#define S16_MAX INT16_MAX
#define S32_MIN INT32_MIN
#define S32_MAX INT32_MAX
#define S64_MIN INT64_MIN
#define S64_MAX INT64_MAX

#define U8_MIN  UINT8_MIN
#define U8_MAX  UINT8_MAX
#define U16_MIN UINT16_MIN
#define U16_MAX UINT16_MAX
#define U32_MIN UINT32_MIN
#define U32_MAX UINT32_MAX
#define U64_MIN UINT64_MIN
#define U64_MAX UINT64_MAX

typedef uint8_t     u8;
typedef int8_t      s8;
typedef volatile u8 vu8;
typedef volatile s8 vs8;

typedef uint16_t     u16;
typedef int16_t      s16;
typedef volatile u16 vu16;
typedef volatile s16 vs16;

typedef uint32_t     u32;
typedef int32_t      s32;
typedef volatile u32 vu32;
typedef volatile s32 vs32;

typedef uint64_t     u64;
typedef int64_t      s64;
typedef volatile u64 vu64;
typedef volatile s64 vs64;

typedef float        f32;
typedef double       f64;
typedef volatile f32 vf32;
typedef volatile f64 vf64;
