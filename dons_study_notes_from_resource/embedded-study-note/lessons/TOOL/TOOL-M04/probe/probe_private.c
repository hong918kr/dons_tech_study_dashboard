/* probe_private.c — an app-side file that tries to use the library's PRIVATE header.
 * With correct usage requirements this must NOT compile. */
#include "rb_internal.h"

int main(void) { return (int)rb_idx(RB_CAP); }
