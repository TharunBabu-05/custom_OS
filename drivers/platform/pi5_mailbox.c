#include "platform_health.h"
#include "trust_time.h"

enum { RX_READ=0x00, RX_STATUS=0x18, TX_WRITE=0x20, TX_STATUS=0x38, PROPERTY_CHANNEL=8 };
#define RX_EMPTY (1u<<30)
#define TX_FULL (1u<<31)
#define RESPONSE_OK (1u<<31)

static enum pi5_reply transfer(struct pi5_mailbox *mailbox,const struct pi5_mailbox_io *io,
                               uintptr_t base,uint64_t timeout_ticks,enum pi5_property property,
                               bool set,uint32_t input,uint32_t *result)
{
    if (mailbox->blocked) { return PI5_REPLY_BLOCKED; }
    if (!timeout_ticks || !mailbox->bus_address || (mailbox->bus_address&15)) { return PI5_REPLY_INVALID; }
    if (!(io->read(base,RX_STATUS)&RX_EMPTY) || (io->read(base,TX_STATUS)&TX_FULL)) {
        return PI5_REPLY_BUSY;
    }

    const bool rtc=property!=PI5_THROTTLED;
    const uint32_t selector=property==PI5_RTC_ALARM_ENABLE ? 3 : property==PI5_RTC_ALARM;
    const uint32_t tag=rtc ? (set ? 0x38087 : 0x30087) : 0x30046;
    const uint32_t value_bytes=rtc ? 8 : 4;
    const uint32_t end=5+value_bytes/4;
    const uint32_t message_bytes=(end+1)*4;
    volatile uint32_t *message=mailbox->message;
    message[0]=message_bytes;
    message[1]=0;
    message[2]=tag;
    message[3]=value_bytes;
    message[4]=0;
    message[5]=selector; /* Throttle reads use a zero sticky-bit clear mask. */
    message[6]=input;
    message[end]=0;
    uint32_t address=mailbox->bus_address|PROPERTY_CHANNEL;
    io->barrier();
    mailbox->reply_length=mailbox->reply_selector=mailbox->reply_value=0;
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
        /* RTC GET can report only its four-byte value. SET may just acknowledge. */
        bool short_reply=rtc && (reply_bytes==4 || (set && !reply_bytes));
        uint32_t returned_selector=message[5];
        mailbox->reply_length=reply_bytes;
        mailbox->reply_selector=returned_selector;
        mailbox->reply_value=message[end-1];
        /* The read uses one tag and one request at a time. Accept a missing
         * selector in word 0 and keep the value in word 1. */
        bool omitted_selector=!set && !returned_selector;
        if (message[0]!=message_bytes || message[2]!=tag || message[3]!=value_bytes ||
            (reply_bytes!=value_bytes && !short_reply) || message[end] ||
            (rtc && returned_selector!=selector && !omitted_selector)) { return PI5_REPLY_INVALID; }
        uint32_t value=mailbox->reply_value;
        /* The firmware alarm-enable field uses bit 0. */
        if (property==PI5_RTC_ALARM_ENABLE) { value&=1; }
        if (result) { *result=value; }
        return PI5_REPLY_OK;
    }
    /* The firmware may still own this buffer. Keep it reserved until reboot. */
    mailbox->blocked=true;
    return PI5_REPLY_TIMEOUT;
}

enum pi5_reply pi5_property_read(struct pi5_mailbox *mailbox,const struct pi5_mailbox_io *io,
                                uintptr_t base,uint64_t timeout_ticks,enum pi5_property property,uint32_t *result)
{
    if ((unsigned)property>PI5_RTC_ALARM_ENABLE || !result) { return PI5_REPLY_INVALID; }
    return transfer(mailbox,io,base,timeout_ticks,property,false,0,result);
}

enum pi5_reply pi5_rtc_set_time(struct pi5_mailbox *mailbox,const struct pi5_mailbox_io *io,
                               uintptr_t base,uint64_t timeout_ticks,uint32_t seconds,uint32_t *verified)
{
    if (seconds<UTC_FIRST_SET || !verified) { return PI5_REPLY_INVALID; }
    enum pi5_reply reply=transfer(mailbox,io,base,timeout_ticks,PI5_RTC_TIME,true,seconds,NULL);
    if (reply!=PI5_REPLY_OK) { return reply; }
    uint32_t readback;
    reply=pi5_property_read(mailbox,io,base,timeout_ticks,PI5_RTC_TIME,&readback);
    if (reply!=PI5_REPLY_OK) { return reply; }
    /* Allow the hardware clock to advance while the two requests complete. */
    if (readback<seconds || readback-seconds>2) { return PI5_REPLY_INVALID; }
    *verified=readback;
    return PI5_REPLY_OK;
}
