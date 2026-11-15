/* Part B: one API, two libraries that both implement it */
#ifndef CRC_H
#define CRC_H
#include <stddef.h>
#include <stdint.h>

uint32_t crc32(const void *buf, size_t len);   /* CRC-32/ISO-HDLC */
const char *crc32_impl(void);                   /* "fast" or "ref" */

struct crc_vec {
	const char *msg;
	uint32_t crc;
};
extern const struct crc_vec crc_vectors[];      /* test vectors, libcrc_ref.a only */
extern const size_t crc_vectors_n;
#endif
