#ifndef OS_FAT32_H
#define OS_FAT32_H
#include "block.h"
#include "ramfs.h"
struct fat32_volume {
    struct block_device device;
    uint32_t start,sectors,fat_lba,fat_sectors,data_lba,clusters,root;
    unsigned spc,fats;
    bool ready;
};
struct fat32_entry { char name[13]; uint32_t cluster,size; bool directory; };
typedef bool (*fat32_emit)(const struct fat32_entry *entry,void *context);
enum fs_result fat32_mount(struct fat32_volume *volume,const struct block_device *device);
enum fs_result fat32_lookup(struct fat32_volume *volume,const char *path,struct fat32_entry *entry);
enum fs_result fat32_list(struct fat32_volume *volume,const char *path,fat32_emit emit,void *context);
enum fs_result fat32_read(struct fat32_volume *volume,const struct fat32_entry *entry,uint32_t offset,
                         void *buffer,size_t capacity,size_t *read_count);
#endif
