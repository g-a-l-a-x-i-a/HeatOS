#ifndef KAT_H
#define KAT_H

#include "types.h"

typedef struct {
    void (*putc)(char c, uint8_t attr);
    void (*puts)(const char *s);
    void (*putln)(const char *s);
} term_hooks_t;

void kat_run(const char *filename, term_hooks_t *hooks);

#endif
