#ifndef OS_PERSIST_H
#define OS_PERSIST_H
#include "block.h"
#include "ramfs.h"
enum { DATA_FILES=8,DATA_FILE_BYTES=4096,DATA_PAYLOAD_BYTES=512+DATA_FILES*DATA_FILE_BYTES,
       DATA_PAYLOAD_SECTORS=DATA_PAYLOAD_BYTES/512,DATA_BANK_SECTORS=1+DATA_PAYLOAD_SECTORS,
       DATA_MIN_SECTORS=1+2*DATA_BANK_SECTORS };
enum { DATA_V2_FILES=16,DATA_V2_FILE_BYTES=8192,DATA_V2_DIRECTORY_BYTES=1024,
       DATA_V2_PAYLOAD_BYTES=DATA_V2_DIRECTORY_BYTES+DATA_V2_FILES*DATA_V2_FILE_BYTES,
       DATA_V2_BANK_SECTORS=1+DATA_V2_PAYLOAD_BYTES/512,DATA_V2_MIN_SECTORS=1+2*DATA_V2_BANK_SECTORS };
enum { DATA_V4_FILES=16,DATA_V4_FILE_BYTES=16384,DATA_V4_DIRECTORY_BYTES=1024,
       DATA_V4_PAYLOAD_BYTES=DATA_V4_DIRECTORY_BYTES+DATA_V4_FILES*DATA_V4_FILE_BYTES,
       DATA_V4_BANK_SECTORS=1+DATA_V4_PAYLOAD_BYTES/512,DATA_V4_MIN_SECTORS=1+2*DATA_V4_BANK_SECTORS };
struct persist_volume {
    struct block_device device;
    uint32_t start,sectors;
    uint64_t generation;
    unsigned bank,valid_banks;
    unsigned version,files,file_bytes,directory_bytes,payload_bytes,bank_sectors;
    bool ready,failed,busy,restoring;
    const char *mount_phase;
    enum block_result mount_io;
    uint64_t mount_lba;
    const char *write_phase;
    enum block_result write_io;
    uint64_t write_lba;
    bool readback_mismatch;
    /* Mutations change the directory and at most one file slot. Keep a
     * compact overlay rather than a second whole-payload RAM allocation. */
    unsigned staged_slot;
    unsigned char live[DATA_V4_PAYLOAD_BYTES];
    unsigned char staged_directory[DATA_V4_DIRECTORY_BYTES],staged_file[DATA_V4_FILE_BYTES];
};
enum persist_bank_state {
    PERSIST_BANK_EMPTY, PERSIST_BANK_BAD_HEADER, PERSIST_BANK_BAD_PAYLOAD,
    PERSIST_BANK_UNREADABLE, PERSIST_BANK_VALID
};
struct persist_bank_report {
    enum persist_bank_state state;
    enum block_result io;
    uint64_t lba,generation;
    bool matches_mounted;
};
struct persist_report {
    unsigned mounted_bank;
    uint64_t mounted_generation;
    struct persist_bank_report banks[2];
};
/* Original bounded snapshot format in its own MBR type-DA partition. No
 * format/resize/raw-write command; only preformatted, verified volumes mount. */
enum fs_result persist_mount(struct persist_volume *,const struct block_device *);
enum fs_result persist_stat(const struct persist_volume *,const char *relative,bool *directory);
enum fs_result persist_list(const struct persist_volume *,const char *,ramfs_emit,void *);
enum fs_result persist_read(const struct persist_volume *,const char *,uint32_t,void *,size_t,size_t *);
enum fs_result persist_write(struct persist_volume *,const char *,const void *,size_t,bool touch);
enum fs_result persist_remove(struct persist_volume *,const char *,bool directory);
enum fs_result persist_rename(struct persist_volume *,const char *source,const char *target);
/* Publish source at target, replacing a regular target in one commit. */
enum fs_result persist_publish(struct persist_volume *,const char *source,const char *target);
enum fs_result persist_mkdir(struct persist_volume *,const char *path);
enum fs_result persist_sync(struct persist_volume *);
enum fs_result persist_check(struct persist_volume *,struct persist_report *);
typedef bool (*persist_emit)(const void *,size_t,void *);
typedef bool (*persist_input)(void *,size_t,void *);
/* A normalized bank-zero header + full payload; no disk writes on export.
 * Restore accepts only an empty, healthy volume of the same format and
 * validates every byte before one existing dual-bank commit. */
enum fs_result persist_export(struct persist_volume *,persist_emit,void *);
enum fs_result persist_export_previous(struct persist_volume *,persist_emit,void *);
enum fs_result persist_restore(struct persist_volume *,size_t,persist_input,void *);
#endif
