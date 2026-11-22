#include "platform_health.h"

enum { RX_READ=0x00, RX_STATUS=0x18, TX_WRITE=0x20, TX_STATUS=0x38, PROPERTY_CHANNEL=8 };
#define RX_EMPTY (1u<<30)
#define TX_FULL (1u<<31)
#define RESPONSE_OK (1u<<31)

enum pi5_reply pi5_property_read(struct pi5_mailbox *mailbox, const struct pi5_mailbox_io *io,
                               uintptr_t base, uint64_t timeout_ticks, enum pi5_property property,
                               uint32_t *result)
{
    if (mailbox->blocked) { return PI5_REPLY_BLOCKED; }
    if (!timeout_ticks || !mailbox->bus_address || (mailbox->bus_address&15) ||
        (property!=PI5_THROTTLED && property!=PI5_RTC_TIME)) { return PI5_REPLY_INVALID; }
    if (!(io->read(base,RX_STATUS)&RX_EMPTY) || (io->read(base,TX_STATUS)&TX_FULL)) {
        return PI5_REPLY_BUSY;
    }

    const bool rtc=property==PI5_RTC_TIME;
    const uint32_t tag=rtc ? 0x30087 : 0x30046;
    const uint32_t value_bytes=rtc ? 8 : 4;
    const uint32_t end=5+value_bytes/4;
    const uint32_t message_bytes=(end+1)*4;
    volatile uint32_t *message=mailbox->message;
    message[0]=message_bytes;
    message[1]=0;
    message[2]=tag;
    message[3]=value_bytes;
    message[4]=0;
    message[5]=0; /* RTC_TIME selector, or a zero sticky-bit clear mask. */
    message[6]=0;
    message[end]=0;
    uint32_t address=mailbox->bus_address|PROPERTY_CHANNEL;
    io->barrier();
    uint64_t start=io->ticks();
    io->write(base,TX_WRITE,address);
    for (unsigned polls=0; polls<2000000 && io->ticks()-start<timeout_ticks; ++polls) {
        if (io->read(base,RX_STATUS)&RX_EMPTY) { continue; }
        if (io->read(base,RX_READ)!=address) {
            mailbox->blocked=true;
            return PI5_REPLY_MISMATCH;
        }
        io->barrier();
        const uint32_t tag_response=message[4];
        if (message[1]!=RESPONSE_OK || !(tag_response&RESPONSE_OK)) { return PI5_REPLY_REJECTED; }
        const uint32_t reply_bytes=tag_response&~RESPONSE_OK;
        /* Pi 5 firmware can report only the four-byte result. RTC_TIME still
         * leaves the selector in word 0 and writes seconds in word 1. */
        if (message[0]!=message_bytes || message[2]!=tag || message[3]!=value_bytes ||
            (reply_bytes!=value_bytes && !(rtc && reply_bytes==4)) || message[end] ||
            (rtc && message[5])) { return PI5_REPLY_INVALID; }
        *result=message[end-1];
        return PI5_REPLY_OK;
    }
    /* The firmware may still own this buffer. Keep it reserved until reboot. */
    mailbox->blocked=true;
    return PI5_REPLY_TIMEOUT;
}
