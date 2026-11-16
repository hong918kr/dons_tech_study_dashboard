/* include/fw_util.h - our own project header (we own it, so the analyzers
 * SHOULD report problems in it).  Helpers shared by the UART link module. */
#ifndef FW_UTIL_H
#define FW_UTIL_H
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#define FW_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* every log line goes through one sink; the format attribute lets the
 * compiler (and clang-tidy's clang-diagnostic-format) check the arguments */
int fw_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Clang thread-safety annotations (no-ops for other compilers).  They only
 * produce diagnostics when the build adds -Wthread-safety. */
#if defined(__clang__)
#define FW_CAPABILITY(x) __attribute__((capability(x)))
#define FW_ACQUIRE(m)    __attribute__((acquire_capability(m)))
#define FW_RELEASE(m)    __attribute__((release_capability(m)))
#define FW_NO_TSA        __attribute__((no_thread_safety_analysis))
#else
#define FW_CAPABILITY(x)
#define FW_ACQUIRE(m)
#define FW_RELEASE(m)
#define FW_NO_TSA
#endif

/* RTOS-style mutex (host build: a pthread mutex underneath) */
typedef struct FW_CAPABILITY("mutex") {
    pthread_mutex_t m;
} fw_mutex_t;
#define FW_MUTEX_INIT {PTHREAD_MUTEX_INITIALIZER}

static inline void fw_mutex_lock(fw_mutex_t *mu) FW_ACQUIRE(mu) FW_NO_TSA
{
    pthread_mutex_lock(&mu->m);
}

static inline void fw_mutex_unlock(fw_mutex_t *mu) FW_RELEASE(mu) FW_NO_TSA
{
    pthread_mutex_unlock(&mu->m);
}

#endif
