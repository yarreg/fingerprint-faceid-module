#ifndef _STATIC_H_
#define _STATIC_H_

#include <stddef.h>

const char *get_static_file(const char *fname, size_t *size);
const char *get_static_text_file(const char *fname);

#endif