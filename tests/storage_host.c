#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "block.h"
#include "fat32.h"
#include "vfs.h"
#include "sdhci.h"
#include "console.h"
#include "memory.h"
#include "heap.h"
#include "dt_property.h"
#include "host_assert.h"
static unsigned char *image; static size_t image_size;
static int64_t fail_lba=-1; static uint64_t reads;
static enum block_result disk_read(void *context,uint64_t lba,unsigned char *sector)
{
    (void)context; assert(lba<image_size/512); ++reads;
    if ((int64_t)lba==fail_lba) { return BLOCK_IO; }
    memcpy(sector,image+lba*512,512); return BLOCK_OK;
}
static struct block_device disk;
_Alignas(4096) static unsigned char pages[32][4096]; static bool allocated[32];
struct page_stats pages_stats(void) { return (struct page_stats){.ready=true}; }
bool page_alloc(uint64_t *physical)
{
    for (unsigned i=0;i<32;++i) { if (!allocated[i]) { allocated[i]=true; *physical=(uintptr_t)pages[i]; return true; } }
    return false;
}
bool page_free(uint64_t physical)
{
    for (unsigned i=0;i<32;++i) { if (physical==(uintptr_t)pages[i] && allocated[i]) { allocated[i]=false; return true; } }
    return false;
}
void console_write(const char *text) { (void)text; }
void console_u64(uint64_t value) { (void)value; }
static void put16(unsigned char *p,uint16_t n) { p[0]=(unsigned char)n; p[1]=(unsigned char)(n>>8); }
static void put32(unsigned char *p,uint32_t n) { put16(p,(uint16_t)n); put16(p+2,(uint16_t)(n>>16)); }
static uint32_t get32(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static unsigned listed;
static bool count_file(const struct fat32_entry *entry,void *context)
{ (void)context; assert(entry->name[0]); ++listed; return true; }
static void fat_set(const struct fat32_volume *v,uint32_t cluster,uint32_t value)
{ for (unsigned i=0;i<v->fats;++i) { put32(image+(v->fat_lba+(uint64_t)i*v->fat_sectors)*512+cluster*4,value); } }
static void entry(unsigned char *p,const char *name,uint32_t cluster,uint32_t size,bool dir)
{ memset(p,0,32); memcpy(p,name,11); p[11]=dir ? 16 : 32; put16(p+20,(uint16_t)(cluster>>16)); put16(p+26,(uint16_t)cluster); put32(p+28,size); }
static void fat_tests(void)
{
    struct fat32_volume v; assert(fat32_mount(&v,&disk)==FS_OK && v.start==2048);
    listed=0; assert(fat32_list(&v,"/",count_file,NULL)==FS_OK && listed>=7);
    struct fat32_entry e; assert(fat32_lookup(&v,"/config.txt",&e)==FS_OK && !e.directory);
    unsigned char data[4096]; size_t n;
    assert(fat32_read(&v,&e,0,data,sizeof(data),&n)==FS_OK && n==e.size);
    assert(n>6 && memcmp(data,"# OurOS",7)==0);
    assert(fat32_lookup(&v,"/OUROS.IMG",&e)==FS_OK && e.size>64);
    assert(fat32_read(&v,&e,0,data,512,&n)==FS_OK && n==512 && get32(data+56)==0x644d5241);
    static struct fat32_file opened;
    assert(fat32_open(&v,"/OUROS.IMG",&opened)==FS_OK && opened.entry.size==e.size);
    assert(fat32_file_read(&opened,0,data,512,&n)==FS_OK && n==512 && get32(data+56)==0x644d5241);
    assert(fat32_file_read(&opened,e.size,data,512,&n)==FS_OK && !n);
    assert(fat32_open(&v,"/missing",&opened)==FS_NOT_FOUND && !opened.ready);
    assert(fat32_file_read(&opened,0,data,1,&n)==FS_NOT_READY);
    if (fat32_lookup(&v,"/FW43455.BIN",&e)==FS_OK) {
        assert(e.size==609309);FILE *firmware=fopen(".dist/wifi-firmware/FW43455.BIN","rb");assert(firmware);
        assert(fat32_open(&v,"/FW43455.BIN",&opened)==FS_OK);unsigned char expected[4096];
        for (uint32_t offset=0;offset<e.size;) {
            assert(fat32_file_read(&opened,offset,data,sizeof(data),&n)==FS_OK && n);
            assert(fread(expected,1,n,firmware)==n && !memcmp(data,expected,n));offset+=(uint32_t)n;
        }
        assert(fgetc(firmware)==EOF);fclose(firmware);
    }
    assert(fat32_lookup(&v,"/missing",&e)==FS_NOT_FOUND);
    assert(fat32_lookup(&v,"/OUROS.IMG/..",&e)==FS_NOT_DIR);
    assert(fat32_lookup(&v,"/..",&e)==FS_INVALID_PATH);
    uint32_t root_cluster=v.root;unsigned char *root=NULL;unsigned slot=0;
    assert(v.spc==1);
    for (uint32_t hop=0;hop<v.clusters;++hop) {
        root=image+(v.data_lba+(uint64_t)(root_cluster-2)*v.spc)*512;
        slot=0;while (slot<16 && root[slot*32]) { ++slot; }if (slot<16) { break; }
        root_cluster=get32(image+v.fat_lba*512ULL+root_cluster*4)&0x0fffffff;assert(root_cluster>=2 && root_cluster<v.clusters+2);
    }assert(slot<16);
    uint32_t folder=200000,c1=200001,c2=200003;
    assert(folder+4<v.clusters && get32(image+v.fat_lba*512ULL+folder*4)==0);
    unsigned char saved_root[512]; memcpy(saved_root,root,512);
    entry(root+slot*32,"TESTDIR    ",folder,0,true);
    /* A full last directory cluster ends at FAT EOC; no extra terminator
     * slot is required. Never overwrite beyond the in-memory cluster. */
    if (slot<15) { root[(slot+1)*32]=0; }
    fat_set(&v,folder,0x0fffffff); fat_set(&v,c1,c2); fat_set(&v,c2,0x0fffffff);
    unsigned char *directory=image+(v.data_lba+(uint64_t)(folder-2)*v.spc)*512;
    entry(directory,"FRAG    TXT",c1,700,false); directory[32]=0;
    unsigned char *a=image+(v.data_lba+(uint64_t)(c1-2)*v.spc)*512;
    unsigned char *b=image+(v.data_lba+(uint64_t)(c2-2)*v.spc)*512;
    memset(a,'A',512); memset(b,'B',512);
    assert(fat32_lookup(&v,"/testdir/frag.txt",&e)==FS_OK);
    assert(fat32_read(&v,&e,500,data,40,&n)==FS_OK && n==40);
    for (unsigned i=0;i<40;++i) { assert(data[i]==(i<12 ? 'A' : 'B')); }
    assert(fat32_open(&v,"/testdir/frag.txt",&opened)==FS_OK);
    uint64_t indexed_before=reads;
    assert(fat32_file_read(&opened,500,data,40,&n)==FS_OK && n==40 && reads-indexed_before==2);
    for (unsigned i=0;i<40;++i) { assert(data[i]==(i<12 ? 'A' : 'B')); }
    fat_set(&v,c1,c1); assert(fat32_read(&v,&e,0,data,700,&n)==FS_CORRUPT);
    assert(fat32_open(&v,"/testdir/frag.txt",&opened)==FS_CORRUPT && !opened.ready);
    fat_set(&v,c1,0x0fffffff); assert(fat32_read(&v,&e,0,data,700,&n)==FS_CORRUPT);
    fat_set(&v,c1,c2); put32(image+(v.fat_lba+(uint64_t)v.fat_sectors)*512+c1*4,0x0ffffff7);
    assert(fat32_read(&v,&e,0,data,700,&n)==FS_CORRUPT); fat_set(&v,c1,c2);
    assert(fat32_open(&v,"/testdir/frag.txt",&opened)==FS_OK);
    fail_lba=v.data_lba+(c1-2)*v.spc;assert(fat32_file_read(&opened,0,data,10,&n)==FS_IO);fail_lba=-1;
    fail_lba=v.data_lba+(c1-2)*v.spc; assert(fat32_read(&v,&e,0,data,10,&n)==FS_IO); fail_lba=-1;
    assert(heap_init() && ramfs_init("test")==FS_OK && heap_check());
    assert(vfs_mount_boot(&disk)==FS_OK); assert(vfs_mount_boot(&disk)==FS_BUSY);
    char path[RAMFS_PATH_MAX]; bool dir;
    assert(vfs_resolve("/","/boot/testdir/../CONFIG.TXT",path,&dir)==FS_OK && !dir && !strcmp(path,"/boot/CONFIG.TXT"));
    assert(vfs_resolve("/boot/TESTDIR","../../home/user",path,&dir)==FS_OK && dir && !strcmp(path,"/home/user"));
    assert(vfs_resolve("/","/boot/CONFIG.TXT/..",path,&dir)==FS_NOT_DIR);
    assert(vfs_resolve("/","/boot/CONFIG.TXT/",path,&dir)==FS_NOT_DIR);
    assert(vfs_prepare_write("/","/boot/new.txt",path)==FS_READ_ONLY);
    assert(vfs_prepare_write("/boot","../tmp/note.txt",path)==FS_OK && !strcmp(path,"/tmp/note.txt"));
    assert(vfs_read("/boot/CONFIG.TXT",0,data,sizeof(data),&n)==FS_OK && n>6);
    assert(vfs_boot_open("/boot/OUROS.IMG",&opened)==FS_OK);
    assert(vfs_boot_open("/etc/board",&opened)==FS_NOT_READY && !opened.ready);
    memcpy(root,saved_root,512); fat_set(&v,folder,0); fat_set(&v,c1,0); fat_set(&v,c2,0);
    uint64_t before=reads; assert(block_read(&disk,disk.sectors,data)==BLOCK_RANGE && reads==before);
    unsigned char *boot=image+2048*512; unsigned char saved_boot[512]; memcpy(saved_boot,boot,512);
    put16(boot+11,4096); assert(fat32_mount(&v,&disk)==FS_UNSUPPORTED); memcpy(boot,saved_boot,512);
    put32(boot+44,UINT32_MAX); assert(fat32_mount(&v,&disk)==FS_CORRUPT); memcpy(boot,saved_boot,512);
    uint32_t size=get32(image+458); put32(image+458,UINT32_MAX); assert(fat32_mount(&v,&disk)==FS_CORRUPT); put32(image+458,size);
    image[510]^=1; assert(fat32_mount(&v,&disk)==FS_UNSUPPORTED); image[510]^=1;
    unsigned char second_partition[16];memcpy(second_partition,image+462,16);
    memcpy(image+462,image+446,16); assert(fat32_mount(&v,&disk)==FS_UNSUPPORTED); memcpy(image+462,second_partition,16);
    fail_lba=0; assert(fat32_mount(&v,&disk)==FS_IO); fail_lba=-1;
    puts("PASS: real-image MBR/FAT32/files, nested fragmented chains, corruption, read failures and VFS mount boundaries");
}
struct mock {
    uint32_t regs[256],data[128]; unsigned position,commands,writes;
    unsigned mode; uint64_t time;
};
static uint32_t mock_read(void *context,unsigned offset)
{
    struct mock *m=context; assert(offset<sizeof(m->regs));
    if (offset==0x20) { assert(m->position<128); uint32_t value=m->data[m->position++]; if (m->position==128) { m->regs[0x30/4]|=2; } return value; }
    return m->regs[offset/4];
}
static void mock_write(void *context,unsigned offset,uint32_t value)
{
    struct mock *m=context; ++m->writes;
    assert(offset==4 || offset==8 || offset==12 || offset==44 || offset==48 || offset==52 || offset==56);
    if (offset==48) { m->regs[offset/4]&=~value; return; }
    m->regs[offset/4]=value;
    if (offset==12) {
        assert(value==((17u<<24)|(0x3au<<16)|0x10u) || value==((5u<<24)|(2u<<16))); ++m->commands; m->position=0;
        if (m->mode==1) { m->regs[0x30/4]=1u<<16; }
        else if (m->mode==2) { m->regs[0x30/4]=1u<<21; }
        else if (m->mode==3) { m->regs[0x30/4]=0; }
        else if (m->mode==4) { m->regs[0x30/4]=1; }
        else if (m->mode==5) { m->regs[0x30/4]=1|32|(1u<<7); }
        else { m->regs[0x30/4]=1|32; }
    }
}
static uint64_t mock_time(void *context) { struct mock *m=context; return ++m->time; }
static void mock_init(struct mock *m)
{
    memset(m,0,sizeof(*m)); m->regs[36/4]=1u<<16; m->regs[40/4]=(1u<<8)|2; m->regs[44/4]=0x0107;
    for (unsigned i=0;i<128;++i) { m->data[i]=i*0x01010101; } m->data[127]=0xaa550000;
}
static void sdhci_tests(void)
{
    struct mock m; mock_init(&m); struct sdhci_host host; struct block_device dev;
    struct sdhci_io io={mock_read,mock_write,mock_time,&m,100};
    m.regs[36/4]|=1u<<17;
    struct sdhci_card_state card=sdhci_card_status(&io);
    assert(card.present && card.stable && card.powered && card.clocked && !card.busy && !card.cqe && !m.writes);
    assert(sdhci_attach(&host,&io,&dev)); assert(host.ready && !dev.capacity_known && dev.sectors==(1ULL<<32));
    assert(m.commands==1 && m.regs[40/4]==((1u<<8)|2) && (m.regs[44/4]&0xffff)==0x0107);
    unsigned char data[512]; assert(block_read(&dev,2048,data)==BLOCK_OK && m.regs[8/4]==2048);
    assert(data[510]==0x55 && data[511]==0xaa);
    m.regs[36/4]=0;
    unsigned writes=m.writes;
    assert(block_read(&dev,0,data)==BLOCK_NOT_READY && !host.ready && m.writes==writes);
    m.regs[36/4]=1u<<16;
    assert(block_read(&dev,0,data)==BLOCK_NOT_READY && m.writes==writes);
    mock_init(&m); assert(sdhci_attach(&host,&io,&dev));
    assert(m.regs[52/4]&(1u<<7));
    m.mode=5;
    assert(block_read(&dev,0,data)==BLOCK_NOT_READY && !host.ready && m.position==0);
    mock_init(&m); assert(sdhci_attach(&host,&io,&dev));
    m.regs[48/4]|=1u<<7; writes=m.writes;
    assert(block_read(&dev,0,data)==BLOCK_NOT_READY && !host.ready && m.writes==writes);
    puts("PASS: read-only microSD status, removal before and during reads, and no stale session reuse after reinsertion");
    for (unsigned mode=1;mode<=4;++mode) {
        mock_init(&m); assert(sdhci_attach(&host,&io,&dev)); m.mode=mode;
        assert(block_read(&dev,0,data)==(mode==2 ? BLOCK_IO : BLOCK_TIMEOUT)); assert(!host.ready);
        unsigned commands=m.commands; assert(block_read(&dev,0,data)==BLOCK_NOT_READY && commands==m.commands);
    }
    mock_init(&m); m.regs[0x208/4]=1; assert(!sdhci_attach(&host,&io,&dev) && !m.writes);
    mock_init(&m); m.regs[44/4]=0; assert(!sdhci_attach(&host,&io,&dev) && !m.writes);
    mock_init(&m); m.regs[36/4]|=3; assert(!sdhci_attach(&host,&io,&dev) && !m.writes);
    mock_init(&m); assert(sdhci_attach(&host,&io,&dev)); m.regs[16/4]=1u<<22;
    assert(block_read(&dev,0,data)==BLOCK_IO && !host.ready);
    mock_init(&m); uint32_t response=0; m.regs[16/4]=0xa0ff8000;
    assert(sdio_query(&io,&response)==BLOCK_OK && response==0xa0ff8000 && m.regs[8/4]==0 && m.commands==1);
    mock_init(&m); m.regs[44/4]=0; assert(sdio_query(&io,&response)==BLOCK_NOT_READY && !m.writes);
    puts("PASS: exact SDHCI PIO engine, CMD17-only commands, preserved bus/clock, timeout/CRC/R1 errors and unsupported CQE");
    puts("PASS: SDIO CMD5 zero-argument query and unclocked-host rejection");
}
struct cold_mock {
    struct mock common;
    bool present,tail;
    unsigned failure,busy_reads;
};
static bool cold_present(void *context) { return ((struct cold_mock *)context)->present; }
static const unsigned cold_commands[]={0,8,55,41,55,41,2,3,9,7};
static void cold_write8(void *context,unsigned offset,uint8_t value)
{
    struct cold_mock *cold=context;
    struct mock *m=&cold->common;
    unsigned reg=40/4;
    ++m->writes;
    if (offset==40) {
        assert(value==0 || value==0x26);
        if (cold->failure!=8 || value!=0x26) { m->regs[reg]=(m->regs[reg]&~0xffu)|value; }
        return;
    }
    assert(offset==41 && (value==0 || value==0x0f));
    if (!value && cold->failure==7) { return; }
    if (!value && !cold->failure) {
        value=0x0e; /* An inactive voltage selector can retain 3.3 V. */
        m->regs[reg]^=1u<<16;
    }
    m->regs[reg]=(m->regs[reg]&~0xff00u)|((uint32_t)value<<8);
}
static uint32_t cold_read(void *context,unsigned offset)
{
    struct cold_mock *cold=context;
    struct mock *m=&cold->common;
    if (offset==36 && cold->busy_reads) {
        --cold->busy_reads;
        return m->regs[offset/4]|2;
    }
    if (!cold->failure && offset==32 && m->position==127) { cold->busy_reads=4; }
    return mock_read(m,offset);
}
static void cold_write(void *context,unsigned offset,uint32_t value)
{
    struct cold_mock *cold=context;
    struct mock *m=&cold->common;
    if (offset==44) { assert(!(m->regs[44/4]&4) || (value&0xffc0)==(m->regs[44/4]&0xffc0)); }
    if (offset==44 && cold->failure==5 && !(value&1)) { return; }
    if (offset==44 && !cold->failure && !(value&1)) { value|=2; } /* Read-only stable bit survives clock disable. */
    if (offset==44 && value&1) {
        assert((value&0xffc0)==250u<<8);
        if (cold->failure!=1) { value|=2; }
        if (value&(3u<<25)) { m->regs[36/4]&=~3u; value&=~(3u<<25); }
    }
    if (offset!=12) { mock_write(m,offset,value); return; }
    const uint32_t args[]={0,0x1aa,0,0x40300000,0,0x40300000,0,0,0x12340000,0x12340000};
    const uint32_t modes[]={0,0x1a,0x1a,2,0x1a,2,9,0x1a,9,0x1b};
    unsigned i=m->commands;
    const unsigned first_read=sizeof(cold_commands)/sizeof(*cold_commands);
    assert(i<first_read+SDHCI_TEST_MAX_SECTORS);
    assert((m->regs[40/4]&0x1f26)==0xf00 && (m->regs[44/4]&7)==7);
    if (i>=first_read) {
        uint32_t lba=(cold->tail ? UINT32_MAX-(SDHCI_TEST_MAX_SECTORS-1) : 0)+(i-first_read);
        assert(m->regs[8/4]==lba);
        assert(value==0x113a0010u);
        if (cold->failure==3 || (cold->failure==6 && lba==1)) { cold->present=false; }
        m->data[0]=lba;
        mock_write(m,offset,value);
        return;
    }
    assert(m->regs[8/4]==args[i]);
    assert(value==((cold_commands[i]<<24)|(modes[i]<<16)));
    ++m->commands;
    m->regs[48/4]=1;
    switch (cold_commands[i]) {
    case 8:
        m->regs[16/4]=0x1aa;
        if (cold->failure==4) { m->regs[48/4]=1u<<16; m->regs[36/4]|=1; }
        break;
    case 55: m->regs[16/4]=32; break;
    case 41: m->regs[16/4]=i==3 ? 0x00ff8000 : cold->failure==2 ? 0x80ff8000 : 0xc0ff8000; break;
    case 3: m->regs[16/4]=0x12340100; break;
    case 9: m->regs[28/4]=1u<<22; m->regs[20/4]=(cold->tail ? 0x3fffff : 31u)<<8; break;
    case 7: m->regs[16/4]=0x700; m->regs[48/4]|=2; break;
    default: break;
    }
}
static void cold_tests(void)
{
    struct cold_mock m;
    const struct sdhci_io io={cold_read,cold_write,mock_time,&m,1000};
    const enum block_result expected[]={BLOCK_OK,BLOCK_TIMEOUT,BLOCK_UNSUPPORTED,BLOCK_NOT_READY,
        BLOCK_TIMEOUT,BLOCK_IO,BLOCK_NOT_READY,BLOCK_IO,BLOCK_IO,BLOCK_OK};
    for (unsigned i=0;i<sizeof(expected)/sizeof(*expected);++i) {
        bool tail=i==9;
        m=(struct cold_mock){.present=true,.tail=tail,.failure=i};
        mock_init(&m.common);
        m.common.regs[36/4]=0x30000; /* The board's GPIO, not this bit, supplies live presence. */
        m.common.regs[40/4]=0x00400026u; m.common.regs[44/4]=0;
        m.common.regs[52/4]=0x40; m.common.regs[56/4]=0x80;
        m.common.regs[64/4]=0x0168c800; m.common.regs[0xfc/4]=0x20000;
        struct sdhci_cold_result out;
        unsigned count=i==0 || i==6 || tail ? SDHCI_TEST_MAX_SECTORS : 1;
        assert(sdhci_cold_test(&io,cold_write8,cold_present,count,tail,&out)==expected[i]);
        assert(out.restore_errors==(i==5 ? SD_RESTORE_CLOCK : i==7 || i==8 ? SD_RESTORE_CONTROL : 0));
        assert(((m.common.regs[44/4]&~2u)==0)==(i!=5));
        assert(out.control_before==0x00400026u && out.control_after==m.common.regs[40/4]);
        assert((out.control_after&0x11ff)==(i==7 ? 0x0126u : i==8 ? 0 : 0x26u));
        assert(out.control_after==(i==0 ? 0x00410e26u : i==7 ? 0x00400f26u : i==8 ? 0x00400000u : 0x00400026u));
        assert(m.common.regs[52/4]==0x40 && m.common.regs[56/4]==0x80 && !(m.common.regs[36/4]&3));
        if (!i || tail) {
            uint32_t hash=2166136261u;
            uint64_t sectors=tail ? 1ULL<<32 : 32768, first=tail ? sectors-count : 0;
            for (unsigned j=0;j<count;++j) {
                for (unsigned n=0;n<512;++n) {
                    uint32_t word=n<4 ? (uint32_t)(first+j) : m.common.data[n/4];
                    hash=(hash^((word>>(8*(n%4)))&255))*16777619u;
                }
            }
            assert(out.command==17 && out.sectors==sectors && out.first_lba==first && out.hash==hash && m.common.position==128);
            assert(m.common.commands==sizeof(cold_commands)/sizeof(*cold_commands)+count);
            assert(out.completed==count && m.common.regs[44/4]==(tail ? 0 : 2) && !m.busy_reads);
        }
        if (i==3) { assert(out.command==17 && m.common.position==0); }
        if (i>=5 && i<=8) { assert(out.command==17 && out.completed==1); }
    }
    m=(struct cold_mock){0};
    struct sdhci_cold_result out;
    assert(sdhci_cold_test(&io,cold_write8,cold_present,0,false,&out)==BLOCK_ARGUMENT && !m.common.writes);
    assert(sdhci_cold_test(&io,cold_write8,cold_present,SDHCI_TEST_MAX_SECTORS+1,false,&out)==BLOCK_ARGUMENT && !m.common.writes);
    assert(sdhci_cold_test(&io,cold_write8,cold_present,1,false,&out)==BLOCK_NOT_READY && !m.common.writes);
    m.present=true;
    assert(sdhci_cold_test(&io,cold_write8,cold_present,1,false,&out)==BLOCK_UNSUPPORTED && !m.common.writes);
    puts("PASS: SD head and SDXC tail hashes, removal and separate control-byte restores, sticky voltage readback and failed power-off detection");
}
struct sd_probe_mock { unsigned reads,fail_at; };
static bool sd_probe_read(void *context,uint64_t address,uint32_t *value)
{
    static const uint64_t addresses[]={0x107d517c04,0x107d517c08,0x1000fff024,0x1000fff028,
        0x1000fff02c,0x1000fff03c,0x1000fff040,0x1000fff0fc,0x1000fff208,0x1000fff444};
    static const uint32_t values[]={16,32,0x30000,0,0,0,0x100c800,0x20000,0,2};
    struct sd_probe_mock *m=context;
    unsigned i=m->reads++;
    assert(i<sizeof(values)/sizeof(*values) && address==addresses[i]);
    if (m->fail_at==m->reads) { return false; }
    *value=values[i];
    return true;
}
static void sd_probe_tests(unsigned char *dtb,size_t bytes)
{
    struct sd_probe_mock m={0};
    struct pi5_sd_snapshot out;
    assert(pi5_sd_snapshot(dtb,bytes,sd_probe_read,&m,&out)==BLOCK_OK && m.reads==10);
    assert(out.gpio_levels==16 && out.gpio_inputs==32 && out.present==0x30000 && !out.control &&
        !out.clock && !out.host_control2 && out.capabilities==0x100c800 && out.version==0x20000 && !out.cqe && out.pin_select==2);
    memset(&out,0xa5,sizeof(out));
    struct pi5_sd_snapshot before=out;
    for (unsigned i=1;i<=10;++i) {
        m=(struct sd_probe_mock){.fail_at=i};
        assert(pi5_sd_snapshot(dtb,bytes,sd_probe_read,&m,&out)==BLOCK_IO && m.reads==i);
        assert(!memcmp(&out,&before,sizeof(out)));
    }
    m=(struct sd_probe_mock){0};
    assert(pi5_sd_snapshot(dtb,bytes-1,sd_probe_read,&m,&out)==BLOCK_UNSUPPORTED && !m.reads);
    const unsigned char *p; uint32_t n;
    static const char *const fields[][2]={
        {"/aliases","mmc0"},{"/soc@107c000000","ranges"},{"/soc@107c000000/mmc@fff000","cd-gpios"},
        {"/soc@107c000000/mmc@fff000","vmmc-supply"},{"/sd-vcc-reg","gpios"},{"/sd-io-1v8-reg","states"}
    };
    for (unsigned i=0;i<sizeof(fields)/sizeof(*fields);++i) {
        assert(dt_property(dtb,bytes,fields[i][0],fields[i][1],&p,&n) && n);
        size_t at=(size_t)(p-dtb)+(i==1 ? 7 : n-1); /* Change the SoC CPU base, not the window length. */
        dtb[at]^=1;
        assert(pi5_sd_snapshot(dtb,bytes,sd_probe_read,&m,&out)==BLOCK_UNSUPPORTED && !m.reads);
        dtb[at]^=1;
    }
    assert(!memcmp(&out,&before,sizeof(out)));
    puts("PASS: Pi 5 microSD DT topology, exact read-only MMIO addresses, altered wiring rejected and incomplete snapshots discarded");
}
static void property_tests(void)
{
    FILE *f=fopen(".dist/rpi5-firmware/bcm2712-rpi-5-b.dtb","rb"); assert(f);
    assert(!fseek(f,0,SEEK_END)); long n=ftell(f); assert(n>40); rewind(f);
    unsigned char *data=malloc((size_t)n); assert(data && fread(data,1,(size_t)n,f)==(size_t)n); fclose(f);
    const unsigned char *value; uint32_t length;
    assert(dt_property(data,(size_t)n,"/aliases","wifi0",&value,&length));
    assert(value[length-1]==0 && strstr((const char *)value,"wifi@1"));
    char path[256]; assert(length<sizeof(path)); memcpy(path,value,length);
    assert(dt_property(data,(size_t)n,path,"compatible",&value,&length));
    assert(!strcmp((const char *)value,"brcm,bcm4329-fmac"));
    assert(!dt_property(data,(size_t)n-1,"/aliases","wifi0",&value,&length));
    assert(!dt_property(data,(size_t)n,"/aliases","nonexistent",&value,&length));
    uint32_t random=0x749913;
    for (unsigned i=0;i<2000;++i) {
        random=random*1664525u+1013904223u; size_t at=random%(size_t)n;
        unsigned char saved=data[at]; data[at]^=(unsigned char)(1u<<(i%8));
        (void)dt_property(data,(size_t)n,"/aliases","wifi0",&value,&length); data[at]=saved;
    }
    puts("PASS: actual Pi DT Wi-Fi alias/binding lookup, truncation and 2000 DT mutations");
    sd_probe_tests(data,(size_t)n);
    free(data);
}
int main(int argc,char **argv)
{
    assert(argc==2); FILE *f=fopen(argv[1],"rb"); assert(f); assert(!fseek(f,0,SEEK_END));
    long n=ftell(f); assert(n==134217728); rewind(f); image_size=(size_t)n;
    image=malloc(image_size); assert(image && fread(image,1,image_size,f)==image_size); fclose(f);
    disk=(struct block_device){.name="host-image",.sectors=image_size/512,.capacity_known=true,.read=disk_read};
    fat_tests(); sdhci_tests(); cold_tests(); property_tests(); free(image); return 0;
}
