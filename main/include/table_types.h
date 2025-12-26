#ifndef _TABLE_TYPES_H_
#define _TABLE_TYPES_H_

#include <stdint.h>

#define TABLE_FINGERPRINT_STRUCT_VERSION 1
#define TABLE_FACE_STRUCT_VERSION 1

typedef struct {
    char name[32];
    bool enabled;
    uint16_t used_count;
    uint32_t last_usage_time;
} table_fingerprint_t;

typedef struct {
    char name[32];
    bool enabled;
    uint16_t used_count;
    uint32_t last_usage_time;
} table_face_t;

#endif
