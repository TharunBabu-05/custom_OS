#ifndef OS_USB_BOT_H
#define OS_USB_BOT_H
#include "block.h"
struct usb_bot_io {
    enum block_result (*transfer)(void *context,bool input,void *data,unsigned length,unsigned *actual);
    void *context;
    enum block_result (*clear_halt)(void *context,bool input);
};
struct usb_bot_trace {
    uint8_t opcode,stage,status;
    uint32_t lba,expected,actual,residue;
};
struct usb_bot {
    struct usb_bot_io io;
    uint32_t tag;
    unsigned stalls_recovered,cache_query;
    uint64_t sectors;
    enum block_result error;
    bool ready;
    bool sense_valid,sense_current,write_through;
    struct usb_bot_trace trace;
    uint8_t sense_key,asc,ascq;
    char vendor[9],product[17];
};
bool usb_bot_attach(struct usb_bot *bot,const struct usb_bot_io *io,struct block_device *device);
enum block_result usb_bot_read(void *context,uint64_t lba,unsigned char *sector);
enum block_result usb_bot_write(void *context,uint64_t lba,const unsigned char *sector);
enum block_result usb_bot_flush(void *context);
#endif
