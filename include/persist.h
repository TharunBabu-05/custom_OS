#ifndef OS_PERSIST_H
#define OS_PERSIST_H
#include "block.h"
#include "ramfs.h"
enum { DATA_FILES=8,DATA_FILE_BYTES=4096,DATA_PAYLOAD_BYTES=512+DATA_FILES*DATA_FILE_BYTES,
       DATA_PAYLOAD_SECTORS=DATA_PAYLOAD_BYTES/512,DATA_BANK_SECTORS=1+DATA_PAYLOAD_SECTORS,
       DATA_MIN_SECTORS=1+2*DATA_BANK_SECTORS };
struct persist_volume {
    struct block_device device;
    uint32_t start,sectors;
    uint64_t generation;
    unsigned bank,valid_banks;
    bool ready,failed,busy;
    unsigned char live[DATA_PAYLOAD_BYTES],staged[DATA_PAYLOAD_BYTES];
};
/* Original bounded snapshot format in its own MBR type-DA partition. No
 * format/resize/raw-write command; only preformatted, verified volumes mount. */
enum fs_result persist_mount(struct persist_volume *,const struct block_device *);
enum fs_result persist_stat(const struct persist_volume *,const char *relative,bool *directory);
enum fs_result persist_list(const struct persist_volume *,const char *,ramfs_emit,void *);
enum fs_result persist_read(const struct persist_volume *,const char *,uint32_t,void *,size_t,size_t *);
enum fs_result persist_write(struct persist_volume *,const char *,const void *,size_t,bool touch);
enum fs_result persist_remove(struct persist_volume *,const char *,bool directory);
enum fs_result persist_sync(struct persist_volume *);
#endif
