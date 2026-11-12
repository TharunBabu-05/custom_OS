/* OurOS data format v1. Two complete SHA256-protected snapshots, ordered
 * invalidate/flush -> payload/flush/readback -> commit/flush/readback. The
 * previous committed bank is never modified during an update. */
#include "persist.h"
#include "sha256.h"
static uint32_t u32(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint64_t u64(const unsigned char *p) { return u32(p)|(uint64_t)u32(p+4)<<32; }
static void put32(unsigned char *p,uint32_t x) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(x>>(8*i)); } }
static void put64(unsigned char *p,uint64_t x) { put32(p,(uint32_t)x);put32(p+4,(uint32_t)(x>>32)); }
static void copy(void *d,const void *s,size_t n) { unsigned char *a=d;const unsigned char *b=s;while (n--) { *a++=*b++; } }
static void zero(void *p,size_t n) { unsigned char *d=p;while (n--) { *d++=0; } }
static bool equal(const void *a,const void *b,size_t n) { const unsigned char *x=a,*y=b;while (n--) { if (*x++!=*y++) { return false; } }return true; }
static bool zeros(const unsigned char *p,size_t n) { while (n--) { if (*p++) { return false; } }return true; }
static void hash(const void *p,size_t n,unsigned char out[32]) { struct sha256 s;sha256_init(&s);sha256_update(&s,p,n);sha256_final(&s,out); }
static bool checksum(const unsigned char *p,size_t n,const unsigned char *expected) { unsigned char h[32];hash(p,n,h);return equal(h,expected,32); }
static bool name_ok(const char *p)
{
    unsigned n=0;while (n<32 && p[n]) { unsigned char c=(unsigned char)p[n];if (c<=32 || c>=127 || c=='/' || c=='\\') { return false; }++n; }
    return n && n<32 && !(n==1 && p[0]=='.') && !(n==2 && p[0]=='.' && p[1]=='.');
}
static enum fs_result name(const char *path,const char **out)
{
    if (!path || path[0]!='/') { return FS_BAD_ARGUMENT; }if (!path[1]) { return FS_IS_DIR; }
    if (!name_ok(path+1)) { return FS_INVALID_PATH; }*out=path+1;return FS_OK;
}
static int find(const unsigned char *payload,const char *key)
{
    for (unsigned i=0;i<DATA_FILES;++i) { const char *p=(const char *)payload+i*64;unsigned n=0;while (n<32 && p[n]==key[n]) { if (!p[n]) { return (int)i; }++n; } }return -1;
}
static bool valid_payload(const unsigned char *p)
{
    for (unsigned i=0;i<DATA_FILES;++i) {
        const unsigned char *e=p+i*64,*data=p+512+i*DATA_FILE_BYTES;
        if (!e[0]) { if (!zeros(e,64) || !zeros(data,DATA_FILE_BYTES)) { return false; }continue; }
        if (!name_ok((const char *)e) || !zeros(e+36,28)) { return false; }
        unsigned n=0;while (e[n]) { ++n; }if (!zeros(e+n,32-n)) { return false; }
        uint32_t length=u32(e+32);if (length>DATA_FILE_BYTES || !zeros(data+length,DATA_FILE_BYTES-length)) { return false; }
        if (find(p,(const char *)e)!=(int)i) { return false; }
    }return true;
}
static bool valid_header(const unsigned char *h,unsigned bank)
{
    return equal(h,"OUROSBK1",8) && u32(h+8)==1 && u32(h+12)==bank && u64(h+16) && u32(h+24)==DATA_PAYLOAD_BYTES &&
        !u32(h+28) && zeros(h+96,416) && checksum(h,64,h+64);
}
static enum block_result mount_read(struct persist_volume *v,uint64_t lba,unsigned char *data)
{ v->mount_lba=lba;v->mount_io=block_read(&v->device,lba,data);return v->mount_io; }
static enum fs_result load(struct persist_volume *v,unsigned bank,unsigned char *data,uint64_t *generation,bool *valid)
{
    unsigned char h[512];uint64_t base=(uint64_t)v->start+1+bank*DATA_BANK_SECTORS;*valid=false;
    v->mount_phase=bank ? "bank1-header" : "bank0-header";
    if (mount_read(v,base,h)!=BLOCK_OK) { return FS_IO; }
    if (!valid_header(h,bank)) { return FS_OK; }
    v->mount_phase=bank ? "bank1-payload" : "bank0-payload";
    for (unsigned i=0;i<DATA_PAYLOAD_SECTORS;++i) { if (mount_read(v,base+1+i,data+i*512)!=BLOCK_OK) { return FS_IO; } }
    *generation=u64(h+16);*valid=checksum(data,DATA_PAYLOAD_BYTES,h+32) && valid_payload(data);return FS_OK;
}
enum fs_result persist_mount(struct persist_volume *v,const struct block_device *device)
{
    if (!v || !device) { return FS_BAD_ARGUMENT; }*v=(struct persist_volume){0};
    v->device=*device;v->mount_phase="capabilities";
    if (!device->read || !device->capacity_known || !device->write || !device->flush) { return FS_UNSUPPORTED; }
    v->mount_phase="mbr";
    unsigned char m[512],h[512];if (mount_read(v,0,m)!=BLOCK_OK) { return FS_IO; }
    if (m[510]!=0x55 || m[511]!=0xaa) { return FS_CORRUPT; }
    unsigned found=0;uint32_t start=0,sectors=0;
    for (unsigned i=0;i<4;++i) {
        const unsigned char *e=m+446+i*16;uint64_t a=u32(e+8),n=u32(e+12);
        if (!e[4]) { if (a || n) { return FS_CORRUPT; }continue; }
        if (a<2048 || !n || a+n>device->sectors || a+n>UINT32_MAX) { return FS_CORRUPT; }
        for (unsigned j=0;j<i;++j) { const unsigned char *p=m+446+j*16;uint64_t b=u32(p+8),size=u32(p+12);if (p[4] && a<b+size && b<a+n) { return FS_CORRUPT; } }
        if (e[4]==0xda) { ++found;start=(uint32_t)a;sectors=(uint32_t)n; }
    }
    if (found!=1 || sectors<DATA_MIN_SECTORS) { return FS_UNSUPPORTED; }
    v->mount_phase="superblock";
    if (mount_read(v,start,h)!=BLOCK_OK) { return FS_IO; }
    if (!equal(h,"OUROSDAT",8) || u32(h+8)!=1 || u32(h+12)!=512 || u32(h+16)!=sectors || u32(h+20)!=DATA_BANK_SECTORS ||
        u32(h+24)!=DATA_FILES || u32(h+28)!=DATA_FILE_BYTES || !zeros(h+32,32) || !zeros(h+96,416) || !checksum(h,64,h+64)) { return FS_CORRUPT; }
    v->device=*device;v->start=start;v->sectors=sectors;bool a,b;uint64_t ga=0,gb=0;
    enum fs_result r=load(v,0,v->live,&ga,&a);if (r!=FS_OK) { return r; }r=load(v,1,v->staged,&gb,&b);if (r!=FS_OK) { return r; }
    v->mount_phase="snapshot-validation";
    if ((!a && !b) || (a && b && ga==gb)) { return FS_CORRUPT; }
    v->valid_banks=(unsigned)a+(unsigned)b;
    if (b && (!a || gb>ga)) { copy(v->live,v->staged,DATA_PAYLOAD_BYTES);v->bank=1;v->generation=gb; }else { v->bank=0;v->generation=ga; }
    zero(v->staged,DATA_PAYLOAD_BYTES);
    /* Check synchronous-flush support before accepting any data writes. */
    v->mount_phase="cache-policy/flush";v->mount_lba=0;v->mount_io=block_flush(&v->device);
    if (v->mount_io!=BLOCK_OK) { return FS_IO; }v->ready=true;v->mount_phase="mounted";return FS_OK;
}
enum fs_result persist_stat(const struct persist_volume *v,const char *path,bool *directory)
{
    if (!v || !directory) { return FS_BAD_ARGUMENT; }if (!v->ready) { return FS_NOT_READY; }
    const char *key;enum fs_result r=name(path,&key);if (r==FS_IS_DIR) { *directory=true;return FS_OK; }if (r!=FS_OK) { return r; }
    if (find(v->live,key)<0) { return FS_NOT_FOUND; }*directory=false;return FS_OK;
}
enum fs_result persist_list(const struct persist_volume *v,const char *path,ramfs_emit emit,void *context)
{
    if (!emit) { return FS_BAD_ARGUMENT; }bool dir;enum fs_result r=persist_stat(v,path,&dir);if (r!=FS_OK) { return r; }
    if (!dir) { const char *key=path+1;int id=find(v->live,key);emit(key,false,u32(v->live+id*64+32),context);return FS_OK; }
    for (unsigned i=0;i<DATA_FILES;++i) { const unsigned char *e=v->live+i*64;if (e[0]) { emit((const char *)e,false,u32(e+32),context); } }return FS_OK;
}
enum fs_result persist_read(const struct persist_volume *v,const char *path,uint32_t offset,void *buffer,size_t capacity,size_t *count)
{
    if (!v || !buffer || !count) { return FS_BAD_ARGUMENT; }*count=0;if (!v->ready) { return FS_NOT_READY; }
    const char *key;enum fs_result r=name(path,&key);if (r!=FS_OK) { return r; }int id=find(v->live,key);if (id<0) { return FS_NOT_FOUND; }
    uint32_t length=u32(v->live+id*64+32);if (offset>=length) { return FS_OK; }size_t n=length-offset;if (n>capacity) { n=capacity; }
    copy(buffer,v->live+512+(unsigned)id*DATA_FILE_BYTES+offset,n);*count=n;return FS_OK;
}
static enum fs_result commit(struct persist_volume *v)
{
    unsigned target=v->bank^1;uint64_t base=(uint64_t)v->start+1+target*DATA_BANK_SECTORS;unsigned char h[512]={0},check[512];
    if (v->generation==UINT64_MAX) { return FS_UNSUPPORTED; }
    /* No write is addressed outside this partition's two fixed banks. */
    if (base<v->start+1ULL || base+DATA_BANK_SECTORS>(uint64_t)v->start+v->sectors) { return FS_CORRUPT; }
    if (block_write(&v->device,base,h)!=BLOCK_OK || block_flush(&v->device)!=BLOCK_OK) { goto io; }
    for (unsigned i=0;i<DATA_PAYLOAD_SECTORS;++i) { if (block_write(&v->device,base+1+i,v->staged+i*512)!=BLOCK_OK) { goto io; } }
    if (block_flush(&v->device)!=BLOCK_OK) { goto io; }
    for (unsigned i=0;i<DATA_PAYLOAD_SECTORS;++i) { if (block_read(&v->device,base+1+i,check)!=BLOCK_OK || !equal(check,v->staged+i*512,512)) { goto io; } }
    copy(h,"OUROSBK1",8);put32(h+8,1);put32(h+12,target);put64(h+16,v->generation+1);put32(h+24,DATA_PAYLOAD_BYTES);
    hash(v->staged,DATA_PAYLOAD_BYTES,h+32);hash(h,64,h+64);
    if (block_write(&v->device,base,h)!=BLOCK_OK || block_flush(&v->device)!=BLOCK_OK || block_read(&v->device,base,check)!=BLOCK_OK || !equal(h,check,512)) { goto io; }
    copy(v->live,v->staged,DATA_PAYLOAD_BYTES);v->bank=target;++v->generation;v->valid_banks=2;zero(v->staged,DATA_PAYLOAD_BYTES);return FS_OK;
io:
    /* Completion may be ambiguous; keep the old RAM view and require reboot
     * before further writes. Mount will select a complete durable generation. */
    v->failed=true;zero(v->staged,DATA_PAYLOAD_BYTES);return FS_IO;
}
enum fs_result persist_write(struct persist_volume *v,const char *path,const void *data,size_t length,bool touch)
{
    if (!v || (length && !data)) { return FS_BAD_ARGUMENT; }if (!v->ready) { return FS_NOT_READY; }if (v->failed) { return FS_IO; }if (v->busy) { return FS_BUSY; }
    if (length>DATA_FILE_BYTES) { return FS_FILE_TOO_LARGE; }const char *key;enum fs_result r=name(path,&key);if (r!=FS_OK) { return r; }
    int id=find(v->live,key);if (id>=0 && touch) { return FS_OK; }
    if (id<0) { for (unsigned i=0;i<DATA_FILES;++i) { if (!v->live[i*64]) { id=(int)i;break; } } }if (id<0) { return FS_NO_NODES; }
    v->busy=true;copy(v->staged,v->live,DATA_PAYLOAD_BYTES);unsigned char *e=v->staged+id*64,*bytes=v->staged+512+id*DATA_FILE_BYTES;
    zero(e,64);unsigned n=0;while (key[n]) { e[n]=(unsigned char)key[n];++n; }put32(e+32,(uint32_t)length);zero(bytes,DATA_FILE_BYTES);copy(bytes,data,length);
    r=commit(v);v->busy=false;return r;
}
enum fs_result persist_remove(struct persist_volume *v,const char *path,bool directory)
{
    if (!v) { return FS_BAD_ARGUMENT; }if (!v->ready) { return FS_NOT_READY; }if (v->failed) { return FS_IO; }if (v->busy) { return FS_BUSY; }
    const char *key;enum fs_result r=name(path,&key);if (r==FS_IS_DIR) { return FS_BUSY; }if (r!=FS_OK) { return r; }if (directory) { return FS_NOT_DIR; }
    int id=find(v->live,key);if (id<0) { return FS_NOT_FOUND; }v->busy=true;copy(v->staged,v->live,DATA_PAYLOAD_BYTES);
    zero(v->staged+id*64,64);zero(v->staged+512+id*DATA_FILE_BYTES,DATA_FILE_BYTES);r=commit(v);v->busy=false;return r;
}
enum fs_result persist_sync(struct persist_volume *v)
{
    if (!v) { return FS_BAD_ARGUMENT; }if (!v->ready) { return FS_NOT_READY; }if (v->failed) { return FS_IO; }if (v->busy) { return FS_BUSY; }
    if (block_flush(&v->device)!=BLOCK_OK) { v->failed=true;return FS_IO; }return FS_OK;
}
