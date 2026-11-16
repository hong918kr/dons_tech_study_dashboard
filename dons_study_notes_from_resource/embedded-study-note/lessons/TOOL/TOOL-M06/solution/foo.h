/* TOOL-M06 libfoo public API — the ONLY header shipped with the library. */
#ifndef FOO_H
#define FOO_H
#include <stddef.h>
#include <stdint.h>

/* TODO(B): FOO_API must export a symbol even when the library is compiled with
 * -fvisibility=hidden (and must be harmless when the app includes this header). */
#define FOO_API __attribute__((visibility("default")))

#ifdef FOO_ABI_V2 /* libfoo 2.x: 'avg' inserted -> layout AND size change */
#define FOO_ABI_VERSION 2
struct foo_stats { uint32_t count; int32_t avg; int32_t min; int32_t max; };
#else             /* libfoo 1.x */
#define FOO_ABI_VERSION 1
struct foo_stats { uint32_t count; int32_t min; int32_t max; };
#endif

typedef struct foo foo_t; /* opaque: apps never depend on sizeof(struct foo) */

FOO_API int foo_abi_check(int app_abi, size_t app_stats_size);
FOO_API foo_t *foo_open(uint32_t window);
FOO_API int32_t foo_push(foo_t *f, int32_t sample); /* moving average, rounded */
FOO_API void foo_get_stats(const foo_t *f, struct foo_stats *out);
FOO_API void foo_close(foo_t *f);
FOO_API const char *foo_version(void);

/* Compiled into the APP: hands the app's compile-time view of the ABI to the lib. */
static inline int foo_init(void)
{
	return foo_abi_check(FOO_ABI_VERSION, sizeof(struct foo_stats));
}

#endif
