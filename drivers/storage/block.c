#include "block.h"
enum block_result block_read(const struct block_device *device,uint64_t lba,void *sector)
{
    if (!device || !device->read || !sector) { return BLOCK_ARGUMENT; }
    if (lba>=device->sectors) { return BLOCK_RANGE; }
    return device->read(device->context,lba,sector);
}
const char *block_error(enum block_result result)
{
    const char *const errors[]={"ok","invalid block argument","sector outside device bounds","controller timeout",
        "controller/card I/O error","unsupported controller/card state","device not ready"};
    return (unsigned)result<sizeof(errors)/sizeof(errors[0]) ? errors[result] : "unknown block error";
}
