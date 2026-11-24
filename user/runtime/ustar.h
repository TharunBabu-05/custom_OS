#ifndef OUROS_USTAR_H
#define OUROS_USTAR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { USTAR_BLOCK=512, USTAR_LIMIT=32768, USTAR_PATH=256, USTAR_MEMBERS=USTAR_LIMIT/USTAR_BLOCK-2 };

struct ustar_member {
    char path[USTAR_PATH];
    uint32_t bytes;
    bool directory;
};

struct ustar_index {
    uint16_t offsets[USTAR_MEMBERS];
    unsigned count;
};

bool ustar_make_header(unsigned char header[USTAR_BLOCK],const char *path,uint32_t bytes,bool directory);
bool ustar_scan(const unsigned char *archive,size_t bytes,struct ustar_index *index);
/* Only call for an offset returned by a successful scan. */
void ustar_member_at(const unsigned char *archive,uint16_t offset,struct ustar_member *member);
#endif
