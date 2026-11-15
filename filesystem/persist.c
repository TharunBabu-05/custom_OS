/* OurOS data formats v1/v2/v3. Two complete SHA256-protected snapshots, ordered
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
static unsigned parent_id(const struct persist_volume *v,const unsigned char *e) { return v->version==3 ? u32(e+40) : 0; }
static bool is_dir(const struct persist_volume *v,const unsigned char *e) { return v->version==3 && u32(e+36)==2; }
static void magic(const struct persist_volume *v,unsigned char *h) { copy(h,"OUROSBK",7);h[7]=(unsigned char)('0'+v->version); }
static int find(const struct persist_volume *v,const unsigned char *payload,const char *key,unsigned parent)
{
    for (unsigned i=0;i<v->files;++i) {
        const unsigned char *e=payload+i*64;if (parent_id(v,e)!=parent) { continue; }
        const char *p=(const char *)e;unsigned n=0;while (n<32 && p[n]==key[n]) { if (!p[n]) { return (int)i; }++n; }
    }return -1;
}
/* Internal paths are absolute within /data; VFS handles dot/dot-dot and cwd.
 * Node ID zero is the implicit root, otherwise ID = slot + 1. */
static enum fs_result lookup(const struct persist_volume *v,const unsigned char *payload,const char *path,int *slot)
{
    if (v->version<3) {
        const char *key;enum fs_result r=name(path,&key);if (r==FS_IS_DIR) { *slot=-1;return FS_OK; }if (r!=FS_OK) { return r; }
        *slot=find(v,payload,key,0);return *slot<0 ? FS_NOT_FOUND : FS_OK;
    }
    if (!path || path[0]!='/') { return FS_BAD_ARGUMENT; }
    unsigned bytes=0;while (bytes<RAMFS_PATH_MAX && path[bytes]) { ++bytes; }if (bytes>RAMFS_PATH_MAX-6) { return FS_PATH_TOO_LONG; }
    unsigned current=0;const char *p=path;
    while (*p) {
        if (current && !is_dir(v,payload+(current-1)*64)) { return FS_NOT_DIR; }
        while (*p=='/') { ++p; }if (!*p) { break; }
        char key[32];unsigned n=0;while (*p && *p!='/') { if (n==31) { return FS_NAME_TOO_LONG; }key[n++]=*p++; }key[n]=0;
        if (!name_ok(key)) { return FS_INVALID_PATH; }int found=find(v,payload,key,current);if (found<0) { return FS_NOT_FOUND; }current=(unsigned)found+1;
    }
    *slot=current ? (int)current-1 : -1;return FS_OK;
}
static enum fs_result target_parent(const struct persist_volume *v,const char *path,unsigned *parent,char key[32])
{
    if (v->version<3) {
        const char *p;enum fs_result r=name(path,&p);if (r!=FS_OK) { return r; }
        zero(key,32);unsigned i=0;do { key[i]=p[i]; }while (p[i++]);*parent=0;return FS_OK;
    }
    if (!path || path[0]!='/') { return FS_BAD_ARGUMENT; }
    unsigned length=0;while (length<RAMFS_PATH_MAX && path[length]) { ++length; }
    if (length>RAMFS_PATH_MAX-6) { return FS_PATH_TOO_LONG; }if (length==1) { return FS_IS_DIR; }
    if (path[length-1]=='/') { return FS_INVALID_PATH; }
    unsigned start=length;while (start && path[start-1]!='/') { --start; }
    if (length-start>31) { return FS_NAME_TOO_LONG; }zero(key,32);copy(key,path+start,length-start);
    if (!name_ok(key)) { return FS_INVALID_PATH; }
    char prefix[RAMFS_PATH_MAX];copy(prefix,path,start);prefix[start]=0;int node;
    enum fs_result r=lookup(v,v->live,prefix,&node);if (r!=FS_OK) { return r; }
    if (node>=0 && !is_dir(v,v->live+(unsigned)node*64)) { return FS_NOT_DIR; }
    *parent=(unsigned)(node+1);return FS_OK;
}
static bool valid_payload(const struct persist_volume *v,const unsigned char *p)
{
    for (unsigned i=0;i<v->files;++i) {
        const unsigned char *e=p+i*64,*data=p+v->directory_bytes+i*v->file_bytes;
        if (!e[0]) { if (!zeros(e,64) || !zeros(data,v->file_bytes)) { return false; }continue; }
        if (!name_ok((const char *)e) || !zeros(e+(v->version==3 ? 44 : 36),v->version==3 ? 20 : 28)) { return false; }
        unsigned n=0;while (e[n]) { ++n; }if (!zeros(e+n,32-n)) { return false; }
        uint32_t length=u32(e+32);if (length>v->file_bytes || !zeros(data+length,v->file_bytes-length)) { return false; }
        if (v->version==3 && ((u32(e+36)!=1 && u32(e+36)!=2) || (is_dir(v,e) && length) || parent_id(v,e)>v->files)) { return false; }
        if (find(v,p,(const char *)e,parent_id(v,e))!=(int)i) { return false; }
    }
    if (v->version==3) { for (unsigned i=0;i<v->files;++i) {
        if (!p[i*64]) { continue; }unsigned parent=parent_id(v,p+i*64),seen=1u<<i,path_bytes=1;
        while (p[i*64+path_bytes-1]) { ++path_bytes; }
        while (parent) {
            if (parent>v->files || (seen&(1u<<(parent-1)))) { return false; }
            seen|=1u<<(parent-1);const unsigned char *e=p+(parent-1)*64;
            if (!e[0] || !is_dir(v,e)) { return false; }
            unsigned n=0;while (e[n]) { ++n; }path_bytes+=1+n;parent=parent_id(v,e);
        }
        /* Include the /data mount prefix: every accepted node must remain
         * reachable through the 255-byte VFS namespace after restore. */
        if (path_bytes>RAMFS_PATH_MAX-6) { return false; }
    } }return true;
}
static bool valid_header(const struct persist_volume *v,const unsigned char *h,unsigned bank)
{
    return equal(h,"OUROSBK",7) && h[7]=='0'+v->version && u32(h+8)==v->version && u32(h+12)==bank && u64(h+16) && u32(h+24)==v->payload_bytes &&
        !u32(h+28) && zeros(h+96,416) && checksum(h,64,h+64);
}
static enum block_result mount_read(struct persist_volume *v,uint64_t lba,unsigned char *data)
{ v->mount_lba=lba;v->mount_io=block_read(&v->device,lba,data);return v->mount_io; }
static enum fs_result load(struct persist_volume *v,unsigned bank,unsigned char *data,uint64_t *generation,bool *valid)
{
    unsigned char h[512];uint64_t base=(uint64_t)v->start+1+bank*v->bank_sectors;*valid=false;
    v->mount_phase=bank ? "bank1-header" : "bank0-header";
    if (mount_read(v,base,h)!=BLOCK_OK) { return FS_IO; }
    if (!valid_header(v,h,bank)) { return FS_OK; }
    v->mount_phase=bank ? "bank1-payload" : "bank0-payload";
    for (unsigned i=0;i<v->payload_bytes/512;++i) { if (mount_read(v,base+1+i,data+i*512)!=BLOCK_OK) { return FS_IO; } }
    *generation=u64(h+16);*valid=checksum(data,v->payload_bytes,h+32) && valid_payload(v,data);return FS_OK;
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
    v->version=u32(h+8);
    if (v->version==1) { v->files=DATA_FILES;v->file_bytes=DATA_FILE_BYTES;v->directory_bytes=512;v->payload_bytes=DATA_PAYLOAD_BYTES;v->bank_sectors=DATA_BANK_SECTORS; }
    else if (v->version==2 || v->version==3) { v->files=DATA_V2_FILES;v->file_bytes=DATA_V2_FILE_BYTES;v->directory_bytes=DATA_V2_DIRECTORY_BYTES;v->payload_bytes=DATA_V2_PAYLOAD_BYTES;v->bank_sectors=DATA_V2_BANK_SECTORS; }
    else { return FS_UNSUPPORTED; }
    if (!equal(h,"OUROSDAT",8) || u32(h+12)!=512 || u32(h+16)!=sectors || u32(h+20)!=v->bank_sectors ||
        u32(h+24)!=v->files || u32(h+28)!=v->file_bytes || sectors<1+2*v->bank_sectors ||
        !zeros(h+32,32) || !zeros(h+96,416) || !checksum(h,64,h+64)) { return FS_CORRUPT; }
    v->device=*device;v->start=start;v->sectors=sectors;bool a,b;uint64_t ga=0,gb=0;
    enum fs_result r=load(v,0,v->live,&ga,&a);if (r!=FS_OK) { return r; }r=load(v,1,v->staged,&gb,&b);if (r!=FS_OK) { return r; }
    v->mount_phase="snapshot-validation";
    if ((!a && !b) || (a && b && ga==gb)) { return FS_CORRUPT; }
    v->valid_banks=(unsigned)a+(unsigned)b;
    if (b && (!a || gb>ga)) { copy(v->live,v->staged,v->payload_bytes);v->bank=1;v->generation=gb; }else { v->bank=0;v->generation=ga; }
    zero(v->staged,sizeof(v->staged));
    /* Check synchronous-flush support before accepting any data writes. */
    v->mount_phase="cache-policy/flush";v->mount_lba=0;v->mount_io=block_flush(&v->device);
    if (v->mount_io!=BLOCK_OK) { return FS_IO; }v->ready=true;v->mount_phase="mounted";return FS_OK;
}
enum fs_result persist_stat(const struct persist_volume *v,const char *path,bool *directory)
{
    if (!v || !directory) { return FS_BAD_ARGUMENT; }if (!v->ready) { return FS_NOT_READY; }
    int id;enum fs_result r=lookup(v,v->live,path,&id);if (r!=FS_OK) { return r; }
    *directory=id<0 || is_dir(v,v->live+(unsigned)id*64);return FS_OK;
}
enum fs_result persist_list(const struct persist_volume *v,const char *path,ramfs_emit emit,void *context)
{
    if (!emit) { return FS_BAD_ARGUMENT; }bool dir;enum fs_result r=persist_stat(v,path,&dir);if (r!=FS_OK) { return r; }
    int id;r=lookup(v,v->live,path,&id);if (r!=FS_OK) { return r; }
    if (!dir) { const unsigned char *e=v->live+(unsigned)id*64;emit((const char *)e,false,u32(e+32),context);return FS_OK; }
    for (unsigned i=0;i<v->files;++i) {
        const unsigned char *e=v->live+i*64;if (e[0] && parent_id(v,e)==(unsigned)(id+1)) { emit((const char *)e,is_dir(v,e),u32(e+32),context); }
    }return FS_OK;
}
enum fs_result persist_read(const struct persist_volume *v,const char *path,uint32_t offset,void *buffer,size_t capacity,size_t *count)
{
    if (!v || !buffer || !count) { return FS_BAD_ARGUMENT; }*count=0;if (!v->ready) { return FS_NOT_READY; }
    int id;enum fs_result r=lookup(v,v->live,path,&id);if (r!=FS_OK) { return r; }if (id<0 || is_dir(v,v->live+(unsigned)id*64)) { return FS_IS_DIR; }
    uint32_t length=u32(v->live+id*64+32);if (offset>=length) { return FS_OK; }size_t n=length-offset;if (n>capacity) { n=capacity; }
    copy(buffer,v->live+v->directory_bytes+(unsigned)id*v->file_bytes+offset,n);*count=n;return FS_OK;
}
static enum fs_result commit(struct persist_volume *v)
{
    unsigned target=v->bank^1;uint64_t base=(uint64_t)v->start+1+target*v->bank_sectors;unsigned char h[512]={0},check[512];
    if (v->generation==UINT64_MAX) { zero(v->staged,sizeof(v->staged));return FS_UNSUPPORTED; }
    /* No write is addressed outside this partition's two fixed banks. */
    if (base<v->start+1ULL || base+v->bank_sectors>(uint64_t)v->start+v->sectors) { return FS_CORRUPT; }
    v->readback_mismatch=false;v->write_phase="invalidate-header";v->write_lba=base;
    if ((v->write_io=block_write(&v->device,base,h))!=BLOCK_OK) { goto io; }
    v->write_phase="invalidate-flush";
    if ((v->write_io=block_flush(&v->device))!=BLOCK_OK) { goto io; }
    v->write_phase="payload-write";
    for (unsigned i=0;i<v->payload_bytes/512;++i) {
        v->write_lba=base+1+i;if ((v->write_io=block_write(&v->device,v->write_lba,v->staged+i*512))!=BLOCK_OK) { goto io; }
    }
    v->write_phase="payload-flush";
    if ((v->write_io=block_flush(&v->device))!=BLOCK_OK) { goto io; }
    v->write_phase="payload-readback";
    for (unsigned i=0;i<v->payload_bytes/512;++i) {
        v->write_lba=base+1+i;if ((v->write_io=block_read(&v->device,v->write_lba,check))!=BLOCK_OK) { goto io; }
        if (!equal(check,v->staged+i*512,512)) { v->readback_mismatch=true;goto io; }
    }
    magic(v,h);put32(h+8,v->version);put32(h+12,target);put64(h+16,v->generation+1);put32(h+24,v->payload_bytes);
    hash(v->staged,v->payload_bytes,h+32);hash(h,64,h+64);
    v->write_phase="commit-header";v->write_lba=base;
    if ((v->write_io=block_write(&v->device,base,h))!=BLOCK_OK) { goto io; }
    v->write_phase="commit-flush";
    if ((v->write_io=block_flush(&v->device))!=BLOCK_OK) { goto io; }
    v->write_phase="commit-readback";
    if ((v->write_io=block_read(&v->device,base,check))!=BLOCK_OK) { goto io; }
    if (!equal(h,check,512)) { v->readback_mismatch=true;goto io; }
    v->write_phase="committed";
    copy(v->live,v->staged,v->payload_bytes);v->bank=target;++v->generation;v->valid_banks=2;zero(v->staged,sizeof(v->staged));return FS_OK;
io:
    /* Completion may be ambiguous; keep the old RAM view and require reboot
     * before further writes. Mount will select a complete durable generation. */
    v->failed=true;zero(v->staged,sizeof(v->staged));return FS_IO;
}
enum fs_result persist_write(struct persist_volume *v,const char *path,const void *data,size_t length,bool touch)
{
    if (!v || (length && !data)) { return FS_BAD_ARGUMENT; }if (!v->ready) { return FS_NOT_READY; }if (v->failed) { return FS_IO; }if (v->busy) { return FS_BUSY; }
    if (length>v->file_bytes) { return FS_FILE_TOO_LARGE; }char key[32]={0};unsigned parent;enum fs_result r=target_parent(v,path,&parent,key);if (r!=FS_OK) { return r; }
    int id=find(v,v->live,key,parent);if (id>=0 && is_dir(v,v->live+(unsigned)id*64)) { return FS_IS_DIR; }if (id>=0 && touch) { return FS_OK; }
    if (id<0) { for (unsigned i=0;i<v->files;++i) { if (!v->live[i*64]) { id=(int)i;break; } } }if (id<0) { return FS_NO_NODES; }
    v->busy=true;copy(v->staged,v->live,v->payload_bytes);unsigned char *e=v->staged+id*64,*bytes=v->staged+v->directory_bytes+id*v->file_bytes;
    zero(e,64);unsigned n=0;while (key[n]) { e[n]=(unsigned char)key[n];++n; }put32(e+32,(uint32_t)length);zero(bytes,v->file_bytes);copy(bytes,data,length);
    if (v->version==3) { put32(e+36,1);put32(e+40,parent); }
    r=commit(v);v->busy=false;return r;
}
enum fs_result persist_remove(struct persist_volume *v,const char *path,bool directory)
{
    if (!v) { return FS_BAD_ARGUMENT; }if (!v->ready) { return FS_NOT_READY; }if (v->failed) { return FS_IO; }if (v->busy) { return FS_BUSY; }
    int id;enum fs_result r=lookup(v,v->live,path,&id);if (r!=FS_OK) { return r; }if (id<0) { return FS_BUSY; }
    bool dir=is_dir(v,v->live+(unsigned)id*64);if (directory && !dir) { return FS_NOT_DIR; }if (!directory && dir) { return FS_IS_DIR; }
    if (dir) { for (unsigned i=0;i<v->files;++i) { if (v->live[i*64] && parent_id(v,v->live+i*64)==(unsigned)id+1) { return FS_NOT_EMPTY; } } }
    v->busy=true;copy(v->staged,v->live,v->payload_bytes);
    zero(v->staged+id*64,64);zero(v->staged+v->directory_bytes+id*v->file_bytes,v->file_bytes);r=commit(v);v->busy=false;return r;
}
enum fs_result persist_sync(struct persist_volume *v)
{
    if (!v) { return FS_BAD_ARGUMENT; }if (!v->ready) { return FS_NOT_READY; }if (v->failed) { return FS_IO; }if (v->busy) { return FS_BUSY; }
    v->write_phase="sync-flush";v->write_lba=0;v->readback_mismatch=false;
    if ((v->write_io=block_flush(&v->device))!=BLOCK_OK) { v->failed=true;return FS_IO; }return FS_OK;
}

enum fs_result persist_mkdir(struct persist_volume *v,const char *path)
{
    if (!v || !path) { return FS_BAD_ARGUMENT; }if (!v->ready) { return FS_NOT_READY; }
    if (v->failed) { return FS_IO; }if (v->busy) { return FS_BUSY; }if (v->version!=3) { return FS_UNSUPPORTED; }
    char key[32]={0};unsigned parent;enum fs_result r=target_parent(v,path,&parent,key);if (r!=FS_OK) { return r; }
    if (find(v,v->live,key,parent)>=0) { return FS_EXISTS; }int id=-1;
    for (unsigned i=0;i<v->files;++i) { if (!v->live[i*64]) { id=(int)i;break; } }if (id<0) { return FS_NO_NODES; }
    v->busy=true;copy(v->staged,v->live,v->payload_bytes);unsigned char *e=v->staged+(unsigned)id*64;
    zero(e,64);unsigned i=0;while (key[i]) { e[i]=(unsigned char)key[i];++i; }put32(e+36,2);put32(e+40,parent);
    r=commit(v);v->busy=false;return r;
}

static enum fs_result move_file(struct persist_volume *v,const char *source,const char *target,bool replace)
{
    if (!v) { return FS_BAD_ARGUMENT; }if (!v->ready) { return FS_NOT_READY; }
    if (v->failed) { return FS_IO; }if (v->busy) { return FS_BUSY; }
    int id;enum fs_result r=lookup(v,v->live,source,&id);if (r!=FS_OK) { return r; }
    if (id<0 || is_dir(v,v->live+(unsigned)id*64)) { return FS_IS_DIR; }
    char next[32]={0};unsigned parent;r=target_parent(v,target,&parent,next);if (r!=FS_OK) { return r; }
    int existing=find(v,v->live,next,parent);if (existing==id) { return FS_OK; }
    if (existing>=0) {
        if (!replace) { return FS_EXISTS; }
        if (is_dir(v,v->live+(unsigned)existing*64)) { return FS_IS_DIR; }
    }
    v->busy=true;copy(v->staged,v->live,v->payload_bytes);unsigned char *e=v->staged+id*64;
    /* The old target and temporary name disappear in the same snapshot that
     * publishes the source. No spare slot or second commit is needed. */
    if (existing>=0) {
        zero(v->staged+(unsigned)existing*64,64);
        zero(v->staged+v->directory_bytes+(unsigned)existing*v->file_bytes,v->file_bytes);
    }
    zero(e,32);unsigned i=0;while (next[i]) { e[i]=(unsigned char)next[i];++i; }
    if (v->version==3) { put32(e+40,parent); }
    r=commit(v);v->busy=false;return r;
}
enum fs_result persist_rename(struct persist_volume *v,const char *source,const char *target)
{ return move_file(v,source,target,false); }
enum fs_result persist_publish(struct persist_volume *v,const char *source,const char *target)
{ return move_file(v,source,target,true); }

enum fs_result persist_export(struct persist_volume *v,persist_emit emit,void *context)
{
    if (!v || !emit) { return FS_BAD_ARGUMENT; }if (!v->ready) { return FS_NOT_READY; }
    if (v->failed) { return FS_IO; }if (v->busy) { return FS_BUSY; }v->busy=true;
    unsigned char h[512]={0};magic(v,h);
    put32(h+8,v->version);put64(h+16,v->generation);put32(h+24,v->payload_bytes);
    hash(v->live,v->payload_bytes,h+32);hash(h,64,h+64);
    bool ok=emit(h,sizeof(h),context);
    for (unsigned i=0;ok && i<v->payload_bytes;i+=512) { ok=emit(v->live+i,512,context); }
    v->busy=false;return ok ? FS_OK : FS_IO;
}
enum fs_result persist_restore(struct persist_volume *v,size_t length,persist_input input,void *context)
{
    if (!v || !input) { return FS_BAD_ARGUMENT; }if (!v->ready) { return FS_NOT_READY; }
    if (v->failed) { return FS_IO; }if (v->busy) { return FS_BUSY; }
    for (unsigned i=0;i<v->files;++i) { if (v->live[i*64]) { return FS_BUSY; } }
    if (length!=512u+v->payload_bytes) { return FS_UNSUPPORTED; }
    v->busy=true;unsigned char h[512];enum fs_result r=FS_IO;
    if (!input(h,512,context)) { goto done; }
    if (!valid_header(v,h,0)) { r=FS_CORRUPT;goto done; }
    for (unsigned i=0;i<v->payload_bytes;i+=512) { if (!input(v->staged+i,512,context)) { goto done; } }
    if (!checksum(v->staged,v->payload_bytes,h+32) || !valid_payload(v,v->staged)) { r=FS_CORRUPT;goto done; }
    r=commit(v);
done:
    zero(v->staged,sizeof(v->staged));v->busy=false;return r;
}
