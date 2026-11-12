#ifndef OS_BLOCK_H
#define OS_BLOCK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum block_result { BLOCK_OK, BLOCK_ARGUMENT, BLOCK_RANGE, BLOCK_TIMEOUT, BLOCK_IO, BLOCK_UNSUPPORTED, BLOCK_NOT_READY, BLOCK_STALL };
struct block_device {
    const char *name;
    uint64_t sectors; /* Bounds, in 512-byte sectors. May be an address limit. */
    bool capacity_known;
    enum block_result (*read)(void *context,uint64_t lba,unsigned char *sector);
    void *context;
    /* Read-only transports leave these NULL. Writes require known capacity
     * and synchronous flush support; users select a bounded data volume. */
    enum block_result (*write)(void *context,uint64_t lba,const unsigned char *sector);
    enum block_result (*flush)(void *context);
};
enum block_result block_read(const struct block_device *device,uint64_t lba,void *sector);
enum block_result block_write(const struct block_device *device,uint64_t lba,const void *sector);
enum block_result block_flush(const struct block_device *device);
const char *block_error(enum block_result result);
bool platform_storage_attach(struct block_device *device);
void platform_storage_info(void);
void storage_setup(const char *board,uintptr_t dtb);
bool storage_command(const char *line);
#endif
