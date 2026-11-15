/* util.h — interface of util.c (declarations only) */
#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include "config.h"

extern const char fw_version[];              /* defined once, in util.c */
uint32_t sectors_for(uint32_t bytes);
uint8_t  checksum8(const uint8_t *p, uint32_t n);

#endif /* UTIL_H */
