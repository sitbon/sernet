#ifndef __LOCAL_H__
#define __LOCAL_H__

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define UART_MAX_LEN 2048


typedef bool        u1;
typedef int8_t      s8;
typedef uint8_t     u8;
typedef int16_t     s16;
typedef uint16_t    u16;
typedef int32_t     s32;
typedef uint32_t    u32;
typedef int64_t     s64;
typedef uint64_t    u64;


static inline void ts_diff(struct timespec *dst, struct timespec *a, struct timespec *b)
{
    if ((b->tv_nsec - a->tv_nsec) < 0) {
        dst->tv_sec = b->tv_sec - a->tv_sec - 1;
        dst->tv_nsec = 1000000000 + b->tv_nsec - a->tv_nsec;
    } else {
        dst->tv_sec = b->tv_sec - a->tv_sec;
        dst->tv_nsec = b->tv_nsec - a->tv_nsec;
    }
}

static inline void ts_add(struct timespec *dst, struct timespec *a, struct timespec *b)
{
    dst->tv_sec = a->tv_sec + b->tv_sec;
    dst->tv_nsec = a->tv_nsec + b->tv_nsec;

    while (dst->tv_nsec >= 1000000000) {
        dst->tv_sec++;
        dst->tv_nsec -= 1000000000;
    }
}

#endif
