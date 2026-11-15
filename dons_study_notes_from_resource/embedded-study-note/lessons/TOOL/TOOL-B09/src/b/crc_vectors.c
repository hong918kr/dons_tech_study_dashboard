/* libcrc_ref.a member 2: known-answer test vectors (separate member!) */
#include "crc.h"

const struct crc_vec crc_vectors[] = {
	{ "", 0x00000000u },
	{ "a", 0xE8B7BE43u },
	{ "123456789", 0xCBF43926u },
};
const size_t crc_vectors_n = sizeof crc_vectors / sizeof crc_vectors[0];
