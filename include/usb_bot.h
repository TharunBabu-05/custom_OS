#ifndef OS_USB_BOT_H
#define OS_USB_BOT_H
#include "block.h"
struct usb_bot_io {
    enum block_result (*transfer)(void *context,bool input,void *data,unsigned length,unsigned *actual);
    void *context;
};
struct usb_bot {
    struct usb_bot_io io;
    uint32_t tag;
    uint64_t sectors;
    enum block_result error;
    bool ready;
    uint8_t sense_key,asc,ascq;
    char vendor[9],product[17];
};
bool usb_bot_attach(struct usb_bot *bot,const struct usb_bot_io *io,struct block_device *device);
enum block_result usb_bot_read(void *context,uint64_t lba,unsigned char *sector);
#endif
