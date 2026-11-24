#include "dt_property.h"
#include "platform_health.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum reply { VALID, SHORT_RTC, NO_REPLY, WRONG_ADDRESS, BAD_LENGTH, BAD_SELECTOR, REFUSED, UNSUPPORTED,
             STALE_TIME, READ_TIMEOUT, ENABLE_FLAGS, SET_ACK, SHORT_ZERO_SELECTOR, ZERO_SELECTOR };
static _Alignas(16) uint32_t message[PI5_MESSAGE_WORDS];
static struct pi5_mailbox mailbox={.message=message,.bus_address=0xc0080000};
static enum reply mode;
static bool pending, tx_full, frozen_counter;
static unsigned writes, barriers, reads;
static uint32_t rtc_time=1795341600;
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
    assert(barriers>0 && message[1]==0 && message[4]==0);
    bool set=message[2]==0x38087,rtc=set || message[2]==0x30087;
    unsigned selector=message[5];
    assert((!selector || (rtc && !set && (selector==1 || selector==3))) && (set || !message[6]));
    assert(message[2]==0x30046 || rtc);
    assert(message[3]==(rtc ? 8u : 4u) && message[0]==(rtc ? 32u : 28u) && message[rtc ? 7 : 6]==0);
    ++writes;
    if (mode==NO_REPLY || mode==WRONG_ADDRESS || (mode==READ_TIMEOUT && !set)) {
        pending=mode==WRONG_ADDRESS;
        return;
    }
    message[1]=mode==REFUSED ? 0x80000001 : 0x80000000;
    uint32_t reply_bytes=message[3];
    if (mode==SHORT_RTC || mode==SHORT_ZERO_SELECTOR) { reply_bytes=4; }
    if (mode==SET_ACK && set) { reply_bytes=0; }
    if (mode==BAD_LENGTH) { reply_bytes=3; }
    message[4]=mode==UNSUPPORTED ? 0 : 0x80000000 | reply_bytes;
    if (rtc) {
        message[5]=mode==BAD_SELECTOR ? selector^1 : selector;
        if (mode==SHORT_ZERO_SELECTOR || mode==ZERO_SELECTOR) { message[5]=0; }
        if (set && mode!=STALE_TIME && mode!=REFUSED && mode!=UNSUPPORTED) { rtc_time=message[6]; }
        if (!set) { message[6]=selector==3 ? (mode==ENABLE_FLAGS ? 2 : 1) : selector==1 ? 1796000000 : rtc_time; }
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

static void clock_test(void)
{
    mailbox.blocked=false; frozen_counter=false; mode=VALID;
    unsigned before=writes;
    uint32_t value=42;
    assert(pi5_rtc_set_time(&mailbox,&io,0x1000,4,0,&value)==PI5_REPLY_INVALID);
    assert(value==42 && writes==before);
    assert(pi5_rtc_set_time(&mailbox,&io,0x1000,4,1796000000,&value)==PI5_REPLY_OK);
    assert(value==1796000000 && writes==before+2 && rtc_time==value);
    mode=SET_ACK;
    assert(pi5_rtc_set_time(&mailbox,&io,0x1000,4,1796000000,&value)==PI5_REPLY_OK && value==1796000000);
    mode=SHORT_RTC;
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_RTC_ALARM,&value)==PI5_REPLY_OK && value==1796000000);
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_RTC_ALARM_ENABLE,&value)==PI5_REPLY_OK && value==1);
    mode=ZERO_SELECTOR;
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_RTC_ALARM_ENABLE,&value)==PI5_REPLY_OK && value==1);
    mode=ENABLE_FLAGS;
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_RTC_ALARM_ENABLE,&value)==PI5_REPLY_OK && value==0);
    mode=SHORT_ZERO_SELECTOR;
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_RTC_ALARM,&value)==PI5_REPLY_OK && value==1796000000);
    assert(pi5_property_read(&mailbox,&io,0x1000,4,PI5_RTC_ALARM_ENABLE,&value)==PI5_REPLY_OK && value==1);
    mode=REFUSED;
    before=writes;
    assert(pi5_rtc_set_time(&mailbox,&io,0x1000,4,1796000100,&value)==PI5_REPLY_REJECTED);
    assert(value==1 && writes==before+1 && rtc_time==1796000000);
    mode=STALE_TIME;
    assert(pi5_rtc_set_time(&mailbox,&io,0x1000,4,1796000100,&value)==PI5_REPLY_INVALID);
    assert(value==1 && writes==before+3 && rtc_time==1796000000);
    mode=READ_TIMEOUT;
    assert(pi5_rtc_set_time(&mailbox,&io,0x1000,4,1796000100,&value)==PI5_REPLY_TIMEOUT);
    assert(value==1 && writes==before+5 && mailbox.blocked);
    assert(pi5_rtc_set_time(&mailbox,&io,0x1000,4,1796000100,&value)==PI5_REPLY_BLOCKED && writes==before+5);
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
    clock_test();
    puts("PASS: Pi 5 health DT wiring, thermal conversion, mailbox reply validation and bounded recovery.");
    puts("PASS: RTC SET readback, short alarm replies, date/enable validation, stale reads and timeout retention.");
    return 0;
}
