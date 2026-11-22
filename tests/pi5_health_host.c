#include "dt_property.h"
#include "platform_health.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum reply { VALID, SHORT_RTC, NO_REPLY, WRONG_ADDRESS, BAD_LENGTH, BAD_SELECTOR, REFUSED, UNSUPPORTED };
static _Alignas(16) uint32_t message[PI5_MESSAGE_WORDS];
static struct pi5_mailbox mailbox={message,0xc0080000,false};
static enum reply mode;
static bool pending, tx_full, frozen_counter;
static unsigned writes, barriers, reads;
static uint64_t count;

static uint32_t read_reg(uintptr_t base, unsigned offset)
{
    assert(base==0x1000);
    switch (offset) {
    case 0x18: return pending ? 0 : 1u<<30;
    case 0x38: return tx_full ? 1u<<31 : 0;
    case 0:
        assert(pending);
        ++reads;
        pending=false;
        return mode==WRONG_ADDRESS ? 0xc0080009 : 0xc0080008;
    default: assert(false); return 0;
    }
}

static void write_reg(uintptr_t base, unsigned offset, uint32_t word)
{
    assert(base==0x1000 && offset==0x20 && word==0xc0080008 && !pending && !tx_full);
    assert(barriers>0 && message[1]==0 && message[4]==0 && message[5]==0 && message[6]==0);
    bool rtc=message[2]==0x30087;
    assert(message[2]==0x30046 || rtc);
    assert(message[3]==(rtc ? 8u : 4u) && message[0]==(rtc ? 32u : 28u) && message[rtc ? 7 : 6]==0);
    ++writes;
    if (mode==NO_REPLY || mode==WRONG_ADDRESS) {
        pending=mode==WRONG_ADDRESS;
        return;
    }
    message[1]=mode==REFUSED ? 0x80000001 : 0x80000000;
    uint32_t reply_bytes=message[3];
    if (mode==SHORT_RTC) { reply_bytes=4; }
    if (mode==BAD_LENGTH) { reply_bytes=3; }
    message[4]=mode==UNSUPPORTED ? 0 : 0x80000000 | reply_bytes;
    if (rtc) {
        message[5]=mode==BAD_SELECTOR ? 1 : 0;
        message[6]=1795341600;
    } else {
        message[5]=0x10001;
    }
    pending=true;
}

static uint64_t ticks(void) { return frozen_counter ? count : count++; }
static void barrier(void) { ++barriers; }
static const struct pi5_mailbox_io io={read_reg,write_reg,ticks,barrier};

static void mailbox_test(void)
{
    uint32_t value=0;
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_RTC_TIME,&value)==PI5_REPLY_OK && value==1795341600);
    mode=SHORT_RTC;
    value=42;
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_RTC_TIME,&value)==PI5_REPLY_OK && value==1795341600);
    mode=VALID;
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_THROTTLED,&value)==PI5_REPLY_OK && value==0x10001);
    assert(writes==3 && reads==3 && barriers==6);
    value=42;
    mode=BAD_LENGTH;
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_RTC_TIME,&value)==PI5_REPLY_INVALID);
    mode=BAD_SELECTOR;
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_RTC_TIME,&value)==PI5_REPLY_INVALID);
    mode=REFUSED;
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_RTC_TIME,&value)==PI5_REPLY_REJECTED);
    mode=UNSUPPORTED;
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_THROTTLED,&value)==PI5_REPLY_REJECTED);
    assert(value==42 && !mailbox.blocked);

    unsigned last_write=writes, last_read=reads;
    pending=true;
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_RTC_TIME,&value)==PI5_REPLY_BUSY);
    pending=false;
    tx_full=true;
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_RTC_TIME,&value)==PI5_REPLY_BUSY);
    tx_full=false;
    assert(pi5_property_read(&mailbox,&io,0x1000,0,PI5_RTC_TIME,&value)==PI5_REPLY_INVALID);
    assert(writes==last_write && reads==last_read);

    mode=NO_REPLY;
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_RTC_TIME,&value)==PI5_REPLY_TIMEOUT);
    pending=true; /* Simulate a late completion received after the deadline. */
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_THROTTLED,&value)==PI5_REPLY_BLOCKED);
    assert(pending && reads==last_read && writes==last_write+1 && value==42);
    pending=false; mailbox.blocked=false;
    mode=WRONG_ADDRESS;
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_RTC_TIME,&value)==PI5_REPLY_MISMATCH);
    assert(mailbox.blocked && value==42);
    mailbox.blocked=false; mode=NO_REPLY; frozen_counter=true;
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_RTC_TIME,&value)==PI5_REPLY_TIMEOUT);
}

int main(int argc,char **argv)
{
    assert(argc==2);
    FILE *file=fopen(argv[1],"rb");
    assert(file);
    static unsigned char dtb[128*1024];
    size_t bytes=fread(dtb,1,sizeof(dtb),file);
    assert(bytes>40 && feof(file) && !ferror(file));
    fclose(file);

    struct pi5_health result=pi5_health_discover(dtb,bytes);
    assert(result.thermal==0x107d542200ULL && result.mailbox==0x107c013880ULL && result.rtc);
    result=pi5_health_discover(dtb,bytes-1);
    assert(!result.thermal && !result.mailbox && !result.rtc);
    const unsigned char *p;
    uint32_t n;
    assert(dt_property(dtb,bytes,"/soc@107c000000/rpi_rtc","firmware",&p,&n) && n==4);
    unsigned char *change=dtb+(p-dtb)+3;
    *change^=1;
    result=pi5_health_discover(dtb,bytes);
    assert(result.thermal && result.mailbox && !result.rtc);
    *change^=1;
    assert(dt_property(dtb,bytes,"/soc@107c000000/avs-monitor@7d542000","status",&p,&n) && n==5);
    memcpy(dtb+(p-dtb),"fail",5);
    result=pi5_health_discover(dtb,bytes);
    assert(!result.thermal && result.mailbox && result.rtc);
    assert(dt_property(dtb,bytes,"/soc@107c000000","ranges",&p,&n) && n==16);
    dtb[p-dtb+7]^=1;
    result=pi5_health_discover(dtb,bytes);
    assert(!result.thermal && !result.mailbox && !result.rtc);

    int32_t temperature=0;
    assert(!pi5_temperature(720,&temperature) && !temperature);
    assert(pi5_temperature((1u<<16)|721,&temperature) && temperature==53450);
    assert(pi5_temperature((1u<<10)|840,&temperature) && temperature==-12000);
    mailbox_test();
    puts("PASS: Pi 5 health DT wiring, thermal conversion, mailbox reply validation and bounded recovery.");
    return 0;
}
