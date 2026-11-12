#include "block.h"
enum block_result block_read(const struct block_device *device,uint64_t lba,void *sector)
{
    if (!device || !device->read || !sector) { return BLOCK_ARGUMENT; }
    if (lba>=device->sectors) { return BLOCK_RANGE; }
    return device->read(device->context,lba,sector);
}
enum block_result block_write(const struct block_device *device,uint64_t lba,const void *sector)
{
    if (!device || !sector) { return BLOCK_ARGUMENT; }
    if (lba>=device->sectors) { return BLOCK_RANGE; }
    if (!device->capacity_known || !device->write || !device->flush) { return BLOCK_UNSUPPORTED; }
    return device->write(device->context,lba,sector);
}
enum block_result block_flush(const struct block_device *device)
{
    if (!device) { return BLOCK_ARGUMENT; }
    if (!device->capacity_known || !device->write || !device->flush) { return BLOCK_UNSUPPORTED; }
    return device->flush(device->context);
}
const char *block_error(enum block_result result)
{
    const char *const errors[]={"ok","invalid block argument","sector outside device bounds","controller timeout",
        "controller/card I/O error","unsupported controller/card state","device not ready","USB endpoint stalled"};
    return (unsigned)result<sizeof(errors)/sizeof(errors[0]) ? errors[result] : "unknown block error";
}
