/* libtests.a member: registers itself from a constructor */
#include "registry.h"

static int test_ring_wrap(void)
{
	unsigned head = 15u;
	return ((head + 1u) & 15u) == 0u ? 0 : 1;
}

__attribute__((constructor)) static void reg_ring(void)
{
	test_register("ring_wrap", test_ring_wrap);
}
