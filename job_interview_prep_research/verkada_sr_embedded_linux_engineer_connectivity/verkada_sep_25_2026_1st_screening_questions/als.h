#ifndef ALS_H
#define ALS_H

#include <stdint.h>

/* Status enum */
enum {
    ERROR = 0,
    NO_CHANGE = 1,
    VALID = 2,
};

/* Result from the vendor api */
typedef struct SensorReading {
    int status;
    float lux;
    uint64_t timestamp; /* microseconds, same clock as get_timestamp() */
} SensorReading;

/* Blocks up to 1 s. Not thread-safe (static state + rand()): call from ONE thread. */
struct SensorReading read_next_sample(void);

/* The pad declared this as get_timestemp(); the definition in main.c is get_timestamp(). */
uint64_t get_timestamp(void);

#endif // ALS_H
