/* Part B app: wants the FAST crc32 and the REF test vectors */
#include <stdio.h>
#include <string.h>
#include "crc.h"

int main(void)
{
	size_t ok = 0;

	for (size_t i = 0; i < crc_vectors_n; i++)
		ok += crc32(crc_vectors[i].msg, strlen(crc_vectors[i].msg)) == crc_vectors[i].crc;
	printf("B: crc32 impl=%s vectors=%zu/%zu ok\n", crc32_impl(), ok, crc_vectors_n);
	return ok == crc_vectors_n ? 0 : 1;
}
