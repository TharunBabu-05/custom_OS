#include <stdio.h>
#include <string.h>
#include "host_assert.h"
#include "block.h"
#include "console.h"
#include "dt_property.h"
#include "irq.h"
#include "reboot.h"
#include "vfs.h"

static char calls[16],message[256];
static unsigned count;
static bool mounted,requested_poweroff;
static enum fs_result flush_result;
static enum block_result stop_result;
static struct reset_target requested;

static void record(char step)
{
    assert(count+1<sizeof(calls));calls[count++]=step;calls[count]='\0';
}
bool vfs_data_mounted(void) { record('m');return mounted; }
enum fs_result vfs_data_sync(void) { record('f');return flush_result; }
enum block_result platform_storage_quiesce(void) { record('q');return stop_result; }
const char *ramfs_error(enum fs_result result) { return result==FS_BUSY ? "busy" : "I/O error"; }
const char *block_error(enum block_result result) { assert(result==BLOCK_TIMEOUT);return "timeout"; }
void console_write(const char *text)
{
    assert(strlen(message)+strlen(text)<sizeof(message));strcat(message,text);
}
void uart_drain(void) { record('d'); }
uint64_t irq_save(void) { record('i');return 0xc0; }
void irq_restore(uint64_t state) { assert(state==0xc0);record('u'); }
void reset_request(const struct reset_target *target,bool poweroff)
{ record('r'); requested=*target; requested_poweroff=poweroff; }

static void run(const char *board,const void *dtb,bool poweroff,bool has_data,enum fs_result flush,
    enum block_result stop,const char *order,const char *feedback)
{
    mounted=has_data;flush_result=flush;stop_result=stop;count=0;calls[0]=message[0]='\0';
    system_power_transition(board,(uintptr_t)dtb,poweroff);
    assert(!strcmp(order,calls) && strstr(message,feedback));
}
static unsigned char *load(const char *path,size_t *length)
{
    FILE *file=fopen(path,"rb");assert(file && !fseek(file,0,SEEK_END));
    long size=ftell(file);assert(size>0 && size<=0x200000 && !fseek(file,0,SEEK_SET));
    unsigned char *data=calloc(1,0x200000);
    assert(data && fread(data,1,(size_t)size,file)==(size_t)size);
    fclose(file);*length=(size_t)size;return data;
}
static void target(const char *file,const char *board,enum reset_method method)
{
    size_t bytes;unsigned char *dtb=load(file,&bytes);
    struct reset_target found;
    assert(reset_discover(board,dtb,bytes,&found) && found.method==method);
    assert(found.registers==(method==RESET_BCM2712 ? 0x107d200000ULL : 0));
    assert(!reset_discover(board,dtb,32,&found));
    for (unsigned off=0;off<2;++off) {
        run(board,dtb,off,true,FS_OK,BLOCK_OK,"mfqdiru",off ? "Halt via " : "Reset via ");
        assert(requested.method==method && requested.registers==found.registers && requested_poweroff==(bool)off);
        run(board,dtb,off,false,FS_IO,BLOCK_OK,"mqdiru","reset request returned");
        run(board,dtb,off,true,FS_IO,BLOCK_OK,"mf","/data flush failed: I/O error");
        run(board,dtb,off,true,FS_BUSY,BLOCK_OK,"mf","/data flush failed: busy");
        run(board,dtb,off,true,FS_OK,BLOCK_TIMEOUT,"mfq","storage quiesce failed: timeout");
        run("unsupported",dtb,off,true,FS_OK,BLOCK_OK,"","no supported reset method");
    }
    const unsigned char *property;uint32_t size;
    const char *node=method==RESET_BCM2712 ? "/soc@107c000000/watchdog@7d200000" : "/psci";
    assert(dt_property(dtb,bytes,node,method==RESET_BCM2712 ? "reg" : "method",&property,&size) && size);
    dtb[property-dtb]^=1;
    assert(!reset_discover(board,dtb,bytes,&found));
    run(board,dtb,false,true,FS_OK,BLOCK_OK,"","no supported reset method");
    free(dtb);
}
int main(int argc,char **argv)
{
    assert(argc==4);
    target(argv[1],"rpi5-header",RESET_BCM2712);
    target(argv[2],"qemu_virt",RESET_PSCI_HVC);
    target(argv[3],"qemu_virt",RESET_PSCI_SMC);
    puts("PASS: Pi watchdog and PSCI validation, reboot and poweroff ordering, no-mount path, and refusal on busy, failed flush or failed quiesce.");
}
