#ifndef OS_BLOCK_H
#define OS_BLOCK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum block_result { BLOCK_OK, BLOCK_ARGUMENT, BLOCK_RANGE, BLOCK_TIMEOUT, BLOCK_IO, BLOCK_UNSUPPORTED, BLOCK_NOT_READY };
struct block_device {
    const char *name;
    uint64_t sectors; /* Bounds, in 512-byte sectors. May be an address limit. */
    bool capacity_known;
    enum block_result (*read)(void *context,uint64_t lba,unsigned char *sector);
    void *context;
};
enum block_result block_read(const struct block_device *device,uint64_t lba,void *sector);
const char *block_error(enum block_result result);
bool platform_storage_attach(struct block_device *device);
void platform_storage_info(void);
void storage_setup(const char *board,uintptr_t dtb);
bool storage_command(const char *line);
#endif
