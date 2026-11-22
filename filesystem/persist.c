/* OurOS data formats v1/v2/v3/v4. Two complete SHA256-protected snapshots, ordered
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
static unsigned parent_id(const struct persist_volume *v,const unsigned char *e) { return v->version>=3 ? u32(e+40) : 0; }
static bool is_dir(const struct persist_volume *v,const unsigned char *e) { return v->version>=3 && u32(e+36)==2; }
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
static bool valid_directory(const struct persist_volume *v,const unsigned char *p)
{
    for (unsigned i=0;i<v->files;++i) {
        const unsigned char *e=p+i*64;
        if (!e[0]) { if (!zeros(e,64)) { return false; }continue; }
        if (!name_ok((const char *)e) || !zeros(e+(v->version>=3 ? 44 : 36),v->version>=3 ? 20 : 28)) { return false; }
        unsigned n=0;while (e[n]) { ++n; }if (!zeros(e+n,32-n)) { return false; }
        uint32_t length=u32(e+32);if (length>v->file_bytes) { return false; }
        if (v->version>=3 && ((u32(e+36)!=1 && u32(e+36)!=2) || (is_dir(v,e) && length) || parent_id(v,e)>v->files)) { return false; }
        if (find(v,p,(const char *)e,parent_id(v,e))!=(int)i) { return false; }
    }
    if (v->version>=3) { for (unsigned i=0;i<v->files;++i) {
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
static bool valid_payload(const struct persist_volume *v,const unsigned char *p)
{
    if (!valid_directory(v,p)) { return false; }
    for (unsigned i=0;i<v->files;++i) {
        unsigned length=u32(p+i*64+32);
        if (!zeros(p+v->directory_bytes+i*v->file_bytes+length,v->file_bytes-length)) { return false; }
    }
    return true;
}
static bool valid_header(const struct persist_volume *v,const unsigned char *h,unsigned bank)
{
    return equal(h,"OUROSBK",7) && h[7]=='0'+v->version && u32(h+8)==v->version && u32(h+12)==bank && u64(h+16) && u32(h+24)==v->payload_bytes &&
        !u32(h+28) && zeros(h+96,416) && checksum(h,64,h+64);
}
static bool scan_read(const struct persist_volume *v,struct persist_bank_report *out,uint64_t lba,unsigned char *data)
{
    enum block_result io=block_read(&v->device,lba,data);
    if (io==BLOCK_OK) { return true; }
    out->state=PERSIST_BANK_UNREADABLE;
    out->io=io;
    out->lba=lba;
    return false;
}
static uint64_t bank_start(const struct persist_volume *v,unsigned bank)
{ return (uint64_t)v->start+1+bank*v->bank_sectors; }

struct extent_index { unsigned char directory[DATA_V5_DIRECTORY_BYTES],extents[DATA_V5_DIRECTORY_BYTES]; };
_Static_assert(sizeof(struct extent_index)==DATA_V5_INDEX_BYTES,"extent index layout");
static unsigned sectors_for(unsigned bytes) { return (bytes+511)/512; }
static bool overlaps(unsigned a,unsigned n,unsigned b,unsigned m)
{ return n && m && a<b+m && b<a+n; }
static bool valid_extents(const struct persist_volume *v,const struct extent_index *index)
{
    for (unsigned i=0;i<v->files;++i) {
        unsigned count=sectors_for(u32(index->directory+i*64+32));
        const unsigned char *row=index->extents+i*64;
        if (!count) { if (!zeros(row,64)) { return false; }continue; }
        unsigned start=u32(row);
        if (start<DATA_V5_FIRST_EXTENT || start>=v->sectors || count>v->sectors-start || !zeros(row+36,28)) { return false; }
        for (unsigned j=0;j<i;++j) {
            if (overlaps(start,count,u32(index->extents+j*64),sectors_for(u32(index->directory+j*64+32)))) { return false; }
        }
    }
    return true;
}
static struct persist_bank_report scan_extents(const struct persist_volume *v,unsigned bank,
    unsigned char header[512],struct extent_index *index)
{
    struct persist_bank_report out={0};
    uint64_t base=bank_start(v,bank);
    if (!scan_read(v,&out,base,header) || zeros(header,512)) { return out; }
    out.state=PERSIST_BANK_BAD_HEADER;
    if (!equal(header,"OUROSEX5",8) || u32(header+8)!=5 || u32(header+12)!=bank || !u64(header+16) ||
        u32(header+24)!=sizeof(*index) || u32(header+28) || !zeros(header+96,416) ||
        !checksum(header,64,header+64)) { return out; }
    out.generation=u64(header+16);out.state=PERSIST_BANK_BAD_PAYLOAD;
    unsigned char *raw=(unsigned char *)index;
    for (unsigned i=0;i<sizeof(*index);i+=512) {
        if (!scan_read(v,&out,base+1+i/512,raw+i)) { return out; }
    }
    if (!checksum(raw,sizeof(*index),header+32) || !valid_directory(v,index->directory) || !valid_extents(v,index)) { return out; }
    for (unsigned i=0;i<v->files;++i) {
        unsigned left=u32(index->directory+i*64+32);
        if (!left) { continue; }
        const unsigned char *row=index->extents+i*64;
        uint64_t lba=(uint64_t)v->start+u32(row);
        struct sha256 sum;sha256_init(&sum);
        unsigned char data[512],digest[32];
        while (left) {
            if (!scan_read(v,&out,lba++,data)) { return out; }
            unsigned used=left<512 ? left : 512;
            if (!zeros(data+used,512-used)) { return out; }
            sha256_update(&sum,data,used);left-=used;
        }
        sha256_final(&sum,digest);
        if (!equal(digest,row+4,sizeof(digest))) { return out; }
    }
    out.state=PERSIST_BANK_VALID;
    out.matches_mounted=bank==v->bank && out.generation==v->generation &&
        equal(index->directory,v->live,sizeof(index->directory)) && equal(index->extents,v->extents,sizeof(index->extents));
    return out;
}

static enum fs_result mount_extents(struct persist_volume *v)
{
    struct extent_index index;
    unsigned char header[512];
    v->mount_phase="extent-validation";
    for (unsigned bank=0;bank<2;++bank) {
        struct persist_bank_report report=scan_extents(v,bank,header,&index);
        if (report.state==PERSIST_BANK_UNREADABLE) { v->mount_io=report.io;v->mount_lba=report.lba;return FS_IO; }
        if (report.state!=PERSIST_BANK_VALID) { continue; }
        ++v->valid_banks;
        if (report.generation==v->generation) { return FS_CORRUPT; }
        if (report.generation>v->generation) {
            v->bank=bank;v->generation=report.generation;
            copy(v->live,index.directory,sizeof(index.directory));copy(v->extents,index.extents,sizeof(index.extents));
        }
    }
    return v->valid_banks ? FS_OK : FS_CORRUPT;
}

/* Verify a bank one sector at a time, leaving the live snapshot available. */
static struct persist_bank_report scan_bank(const struct persist_volume *v,unsigned bank,unsigned char *header)
{
    struct persist_bank_report out={0};
    uint64_t base=bank_start(v,bank);
    if (!scan_read(v,&out,base,header) || zeros(header,512)) { return out; }
    out.state=PERSIST_BANK_BAD_HEADER;
    if (!valid_header(v,header,bank)) { return out; }
    out.generation=u64(header+16);
    out.state=PERSIST_BANK_BAD_PAYLOAD;

    unsigned char directory[DATA_V4_DIRECTORY_BYTES],sector[512],digest[32];
    for (unsigned i=0;i<v->directory_bytes;i+=512) {
        if (!scan_read(v,&out,base+1+i/512,directory+i)) { return out; }
    }
    if (!valid_directory(v,directory)) { return out; }
    bool matches=bank==v->bank && out.generation==v->generation &&
        equal(directory,v->live,v->directory_bytes);
    struct sha256 sum;
    sha256_init(&sum);
    sha256_update(&sum,directory,v->directory_bytes);
    for (unsigned slot=0;slot<v->files;++slot) {
        unsigned length=u32(directory+slot*64+32);
        unsigned file=v->directory_bytes+slot*v->file_bytes;
        for (unsigned offset=0;offset<v->file_bytes;offset+=512) {
            unsigned at=file+offset;
            if (!scan_read(v,&out,base+1+at/512,sector)) { return out; }
            unsigned used=offset<length ? length-offset : 0;
            if (used<512 && !zeros(sector+used,512-used)) { return out; }
            sha256_update(&sum,sector,sizeof(sector));
            matches=matches && equal(sector,v->live+at,sizeof(sector));
        }
    }
    sha256_final(&sum,digest);
    if (equal(digest,header+32,sizeof(digest))) {
        out.state=PERSIST_BANK_VALID;
        out.matches_mounted=matches;
    }
    return out;
}
enum fs_result persist_check(struct persist_volume *v,struct persist_report *report)
{
    if (!v || !report) { return FS_BAD_ARGUMENT; }
    if (!v->ready) { return FS_NOT_READY; }
    if (v->busy) { return FS_BUSY; }
    unsigned char header[512];
    v->busy=true;
    report->mounted_bank=v->bank;
    report->mounted_generation=v->generation;
    for (unsigned bank=0;bank<2;++bank) {
        if (v->version==5) { struct extent_index index;report->banks[bank]=scan_extents(v,bank,header,&index); }
        else { report->banks[bank]=scan_bank(v,bank,header); }
    }
    v->busy=false;
    return FS_OK;
}
static enum block_result mount_read(struct persist_volume *v,uint64_t lba,unsigned char *data)
{ v->mount_lba=lba;v->mount_io=block_read(&v->device,lba,data);return v->mount_io; }
static enum fs_result load(struct persist_volume *v,unsigned bank,uint64_t *generation,bool *valid,unsigned char digest[32],bool *overwritten)
{
    unsigned char h[512];uint64_t base=bank_start(v,bank);*valid=false;*overwritten=false;
    v->mount_phase=bank ? "bank1-header" : "bank0-header";
    if (mount_read(v,base,h)!=BLOCK_OK) { return FS_IO; }
    if (!valid_header(v,h,bank)) { return FS_OK; }
    *overwritten=true;
    v->mount_phase=bank ? "bank1-payload" : "bank0-payload";
    for (unsigned i=0;i<v->payload_bytes/512;++i) { if (mount_read(v,base+1+i,v->live+i*512)!=BLOCK_OK) { return FS_IO; } }
    *generation=u64(h+16);copy(digest,h+32,32);*valid=checksum(v->live,v->payload_bytes,h+32) && valid_payload(v,v->live);return FS_OK;
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
    else if (v->version==4) { v->files=DATA_V4_FILES;v->file_bytes=DATA_V4_FILE_BYTES;v->directory_bytes=DATA_V4_DIRECTORY_BYTES;v->payload_bytes=DATA_V4_PAYLOAD_BYTES;v->bank_sectors=DATA_V4_BANK_SECTORS; }
    else if (v->version==5) {
        if (sectors!=DATA_V5_SECTORS) { return FS_CORRUPT; }
        v->files=DATA_V5_FILES;v->file_bytes=DATA_V5_FILE_BYTES;v->directory_bytes=DATA_V5_DIRECTORY_BYTES;
        v->payload_bytes=DATA_V5_INDEX_BYTES;v->bank_sectors=DATA_V5_BANK_SECTORS;
    }
    else { return FS_UNSUPPORTED; }
    if (!equal(h,"OUROSDAT",8) || u32(h+12)!=512 || u32(h+16)!=sectors || u32(h+20)!=v->bank_sectors ||
        u32(h+24)!=v->files || u32(h+28)!=v->file_bytes || sectors<1+2*v->bank_sectors ||
        !zeros(h+32,32) || !zeros(h+96,416) || !checksum(h,64,h+64)) { return FS_CORRUPT; }
    v->device=*device;v->start=start;v->sectors=sectors;
    if (v->version==5) {
        enum fs_result r=mount_extents(v);if (r!=FS_OK) { return r; }
    } else {
    bool a,b;uint64_t ga=0,gb=0;
    unsigned char ha[32],hb[32];bool changed;enum fs_result r=load(v,0,&ga,&a,ha,&changed);if (r!=FS_OK) { return r; }r=load(v,1,&gb,&b,hb,&changed);if (r!=FS_OK) { return r; }
    v->mount_phase="snapshot-validation";
    if ((!a && !b) || (a && b && ga==gb)) { return FS_CORRUPT; }
    v->valid_banks=(unsigned)a+(unsigned)b;
    if (b && (!a || gb>ga)) { v->bank=1;v->generation=gb; }
    else {
        /* Bank 1 may have overwritten the work buffer. Re-read and verify the
         * exact previously validated bank-0 identity before publishing it. */
        if (changed) {
            uint64_t again=0;bool valid=false,overwritten;unsigned char h[32];r=load(v,0,&again,&valid,h,&overwritten);
            if (r!=FS_OK) { return r; }if (!valid || again!=ga || !equal(h,ha,32)) { return FS_CORRUPT; }
        }
        v->bank=0;v->generation=ga;
    }
    }
    v->staged_slot=v->files;
    /* Check synchronous-flush support before accepting any data writes. */
    v->mount_phase="cache-policy/flush";v->mount_lba=0;v->mount_io=block_flush(&v->device);
    if (v->mount_io!=BLOCK_OK) { return FS_IO; }v->ready=true;v->mount_phase="mounted";return FS_OK;
}
enum fs_result persist_stat(const struct persist_volume *v,const char *path,bool *directory)
{
    if (!v || !directory) { return FS_BAD_ARGUMENT; }if (!v->ready) { return FS_NOT_READY; }if (v->restoring) { return FS_BUSY; }
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
    if (!v || !buffer || !count) { return FS_BAD_ARGUMENT; }*count=0;if (!v->ready) { return FS_NOT_READY; }if (v->restoring) { return FS_BUSY; }
    int id;enum fs_result r=lookup(v,v->live,path,&id);if (r!=FS_OK) { return r; }if (id<0 || is_dir(v,v->live+(unsigned)id*64)) { return FS_IS_DIR; }
    uint32_t length=u32(v->live+id*64+32);if (offset>=length) { return FS_OK; }size_t n=length-offset;if (n>capacity) { n=capacity; }
    if (v->version==5) {
        uint64_t base=(uint64_t)v->start+u32(v->extents+(unsigned)id*64);
        size_t done=0;unsigned char sector[512];
        while (done<n) {
            unsigned within=offset%512;size_t take=512-within;if (take>n-done) { take=n-done; }
            if (block_read(&v->device,base+offset/512,sector)!=BLOCK_OK) { return FS_IO; }
            copy((unsigned char *)buffer+done,sector+within,take);done+=take;offset+=(uint32_t)take;
        }
        *count=n;return FS_OK;
    }
    copy(buffer,v->live+v->directory_bytes+(unsigned)id*v->file_bytes+offset,n);*count=n;return FS_OK;
}
static void stage_clear(struct persist_volume *v)
{ zero(v->staged_directory,sizeof(v->staged_directory));zero(v->staged_file,sizeof(v->staged_file));zero(v->staged_extents,sizeof(v->staged_extents));v->staged_slot=v->files; }
static void stage_begin(struct persist_volume *v)
{
    stage_clear(v);copy(v->staged_directory,v->live,v->directory_bytes);
    if (v->version==5) { copy(v->staged_extents,v->extents,sizeof(v->extents)); }
}
static const unsigned char *stage_sector(const struct persist_volume *v,unsigned sector)
{
    unsigned offset=sector*512;if (offset<v->directory_bytes) { return v->staged_directory+offset; }
    offset-=v->directory_bytes;unsigned slot=offset/v->file_bytes;
    return slot==v->staged_slot ? v->staged_file+offset%v->file_bytes : v->live+v->directory_bytes+offset;
}
static void stage_hash(const struct persist_volume *v,unsigned char digest[32])
{
    struct sha256 s;sha256_init(&s);sha256_update(&s,v->staged_directory,v->directory_bytes);
    for (unsigned i=0;i<v->files;++i) { sha256_update(&s,i==v->staged_slot ? v->staged_file : v->live+v->directory_bytes+i*v->file_bytes,v->file_bytes); }
    sha256_final(&s,digest);
}
static unsigned find_extent(const struct persist_volume *v,unsigned count)
{
    unsigned at=DATA_V5_FIRST_EXTENT;
    for (;;) {
        if (at>v->sectors-count) { return 0; }
        unsigned next=at;
        for (unsigned i=0;i<v->files;++i) {
            unsigned start=u32(v->extents+i*64),used=sectors_for(u32(v->live+i*64+32));
            if (overlaps(at,count,start,used) && start+used>next) { next=start+used; }
        }
        if (next==at) { return at; }
        at=next;
    }
}
static bool extent_write(struct persist_volume *v,uint64_t lba,const unsigned char *data)
{
    v->write_lba=lba;v->write_io=block_write(&v->device,lba,data);
    return v->write_io==BLOCK_OK;
}
static bool extent_flush(struct persist_volume *v,const char *phase)
{
    v->write_phase=phase;v->write_io=block_flush(&v->device);
    return v->write_io==BLOCK_OK;
}
static bool extent_match(struct persist_volume *v,uint64_t lba,const unsigned char *expected)
{
    unsigned char data[512];v->write_lba=lba;v->write_io=block_read(&v->device,lba,data);
    if (v->write_io!=BLOCK_OK) { return false; }
    v->readback_mismatch=!equal(data,expected,512);
    return !v->readback_mismatch;
}
static bool extent_begin(struct persist_volume *v)
{
    unsigned char invalid[512]={0};
    v->readback_mismatch=false;v->write_phase="invalidate-header";
    if (!extent_write(v,bank_start(v,v->bank^1),invalid) || !extent_flush(v,"invalidate-flush")) { return false; }
    /* Read back the invalidation before reusing sectors referenced only by
     * the alternate generation. No data reachable from the live bank moves. */
    v->write_phase="invalidate-readback";
    if (!extent_match(v,bank_start(v,v->bank^1),invalid)) { return false; }
    v->valid_banks=1;return true;
}
static bool extent_write_file(struct persist_volume *v,unsigned at,unsigned length)
{
    unsigned blocks=sectors_for(length);uint64_t base=(uint64_t)v->start+at;
    v->write_phase="file-write";
    for (unsigned i=0;i<blocks;++i) { if (!extent_write(v,base+i,v->staged_file+i*512)) { return false; } }
    if (!extent_flush(v,"file-flush")) { return false; }
    v->write_phase="file-readback";
    for (unsigned i=0;i<blocks;++i) { if (!extent_match(v,base+i,v->staged_file+i*512)) { return false; } }
    return true;
}
static bool extent_publish(struct persist_volume *v)
{
    unsigned target=v->bank^1;uint64_t base=bank_start(v,target);
    struct extent_index index;
    copy(index.directory,v->staged_directory,sizeof(index.directory));copy(index.extents,v->staged_extents,sizeof(index.extents));
    const unsigned char *raw=(const unsigned char *)&index;
    v->write_phase="index-write";
    for (unsigned i=0;i<sizeof(index);i+=512) { if (!extent_write(v,base+1+i/512,raw+i)) { return false; } }
    if (!extent_flush(v,"index-flush")) { return false; }
    v->write_phase="index-readback";
    for (unsigned i=0;i<sizeof(index);i+=512) { if (!extent_match(v,base+1+i/512,raw+i)) { return false; } }
    unsigned char header[512]={0};copy(header,"OUROSEX5",8);
    put32(header+8,5);put32(header+12,target);put64(header+16,v->generation+1);put32(header+24,sizeof(index));
    hash(raw,sizeof(index),header+32);hash(header,64,header+64);
    v->write_phase="commit-header";
    if (!extent_write(v,base,header) || !extent_flush(v,"commit-flush")) { return false; }
    v->write_phase="commit-readback";
    if (!extent_match(v,base,header)) { return false; }
    v->write_phase="committed";
    copy(v->live,index.directory,sizeof(index.directory));copy(v->extents,index.extents,sizeof(index.extents));
    v->bank=target;++v->generation;v->valid_banks=2;return true;
}
static enum fs_result commit_extents(struct persist_volume *v)
{
    unsigned length=0,at=0;
    if (v->staged_slot<v->files) {
        unsigned char *row=v->staged_extents+v->staged_slot*64;zero(row,64);
        length=u32(v->staged_directory+v->staged_slot*64+32);
        if (length) {
            at=find_extent(v,sectors_for(length));
            if (!at) { stage_clear(v);return FS_NO_SPACE; }
            put32(row,at);hash(v->staged_file,length,row+4);
        }
    }
    bool ok=extent_begin(v) && (!length || extent_write_file(v,at,length)) && extent_publish(v);
    if (!ok) { v->failed=true; }
    stage_clear(v);return ok ? FS_OK : FS_IO;
}
static enum fs_result commit(struct persist_volume *v)
{
    if (v->generation==UINT64_MAX) { stage_clear(v);return FS_UNSUPPORTED; }
    if (v->version==5) { return commit_extents(v); }
    unsigned target=v->bank^1;uint64_t base=bank_start(v,target);unsigned char h[512]={0},check[512];
    /* No write is addressed outside this partition's two fixed banks. */
    if (base<v->start+1ULL || base+v->bank_sectors>(uint64_t)v->start+v->sectors) { return FS_CORRUPT; }
    v->readback_mismatch=false;v->write_phase="invalidate-header";v->write_lba=base;
    if ((v->write_io=block_write(&v->device,base,h))!=BLOCK_OK) { goto io; }
    v->write_phase="invalidate-flush";
    if ((v->write_io=block_flush(&v->device))!=BLOCK_OK) { goto io; }
    v->write_phase="payload-write";
    for (unsigned i=0;i<v->payload_bytes/512;++i) {
        v->write_lba=base+1+i;if ((v->write_io=block_write(&v->device,v->write_lba,stage_sector(v,i)))!=BLOCK_OK) { goto io; }
    }
    v->write_phase="payload-flush";
    if ((v->write_io=block_flush(&v->device))!=BLOCK_OK) { goto io; }
    v->write_phase="payload-readback";
    for (unsigned i=0;i<v->payload_bytes/512;++i) {
        v->write_lba=base+1+i;if ((v->write_io=block_read(&v->device,v->write_lba,check))!=BLOCK_OK) { goto io; }
        if (!equal(check,stage_sector(v,i),512)) { v->readback_mismatch=true;goto io; }
    }
    magic(v,h);put32(h+8,v->version);put32(h+12,target);put64(h+16,v->generation+1);put32(h+24,v->payload_bytes);
    stage_hash(v,h+32);hash(h,64,h+64);
    v->write_phase="commit-header";v->write_lba=base;
    if ((v->write_io=block_write(&v->device,base,h))!=BLOCK_OK) { goto io; }
    v->write_phase="commit-flush";
    if ((v->write_io=block_flush(&v->device))!=BLOCK_OK) { goto io; }
    v->write_phase="commit-readback";
    if ((v->write_io=block_read(&v->device,base,check))!=BLOCK_OK) { goto io; }
    if (!equal(h,check,512)) { v->readback_mismatch=true;goto io; }
    v->write_phase="committed";
    copy(v->live,v->staged_directory,v->directory_bytes);
    if (v->staged_slot<v->files) { copy(v->live+v->directory_bytes+v->staged_slot*v->file_bytes,v->staged_file,v->file_bytes); }
    v->bank=target;++v->generation;v->valid_banks=2;stage_clear(v);return FS_OK;
io:
    /* Completion may be ambiguous; keep the old RAM view and require reboot
     * before further writes. Mount will select a complete durable generation. */
    v->failed=true;stage_clear(v);return FS_IO;
}
enum fs_result persist_write(struct persist_volume *v,const char *path,const void *data,size_t length,bool touch)
{
    if (!v || (length && !data)) { return FS_BAD_ARGUMENT; }if (!v->ready) { return FS_NOT_READY; }if (v->failed) { return FS_IO; }if (v->busy) { return FS_BUSY; }
    if (length>v->file_bytes) { return FS_FILE_TOO_LARGE; }char key[32]={0};unsigned parent;enum fs_result r=target_parent(v,path,&parent,key);if (r!=FS_OK) { return r; }
    int id=find(v,v->live,key,parent);if (id>=0 && is_dir(v,v->live+(unsigned)id*64)) { return FS_IS_DIR; }if (id>=0 && touch) { return FS_OK; }
    if (id<0) { for (unsigned i=0;i<v->files;++i) { if (!v->live[i*64]) { id=(int)i;break; } } }if (id<0) { return FS_NO_NODES; }
    v->busy=true;stage_begin(v);v->staged_slot=(unsigned)id;unsigned char *e=v->staged_directory+id*64,*bytes=v->staged_file;
    zero(e,64);unsigned n=0;while (key[n]) { e[n]=(unsigned char)key[n];++n; }put32(e+32,(uint32_t)length);zero(bytes,v->file_bytes);copy(bytes,data,length);
    if (v->version>=3) { put32(e+36,1);put32(e+40,parent); }
    r=commit(v);v->busy=false;return r;
}
enum fs_result persist_remove(struct persist_volume *v,const char *path,bool directory)
{
    if (!v) { return FS_BAD_ARGUMENT; }if (!v->ready) { return FS_NOT_READY; }if (v->failed) { return FS_IO; }if (v->busy) { return FS_BUSY; }
    int id;enum fs_result r=lookup(v,v->live,path,&id);if (r!=FS_OK) { return r; }if (id<0) { return FS_BUSY; }
    bool dir=is_dir(v,v->live+(unsigned)id*64);if (directory && !dir) { return FS_NOT_DIR; }if (!directory && dir) { return FS_IS_DIR; }
    if (dir) { for (unsigned i=0;i<v->files;++i) { if (v->live[i*64] && parent_id(v,v->live+i*64)==(unsigned)id+1) { return FS_NOT_EMPTY; } } }
    v->busy=true;stage_begin(v);
    zero(v->staged_directory+id*64,64);v->staged_slot=(unsigned)id;r=commit(v);v->busy=false;return r;
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
    if (v->failed) { return FS_IO; }if (v->busy) { return FS_BUSY; }if (v->version<3) { return FS_UNSUPPORTED; }
    char key[32]={0};unsigned parent;enum fs_result r=target_parent(v,path,&parent,key);if (r!=FS_OK) { return r; }
    if (find(v,v->live,key,parent)>=0) { return FS_EXISTS; }int id=-1;
    for (unsigned i=0;i<v->files;++i) { if (!v->live[i*64]) { id=(int)i;break; } }if (id<0) { return FS_NO_NODES; }
    v->busy=true;stage_begin(v);unsigned char *e=v->staged_directory+(unsigned)id*64;
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
    v->busy=true;stage_begin(v);unsigned char *e=v->staged_directory+id*64;
    /* The old target and temporary name disappear in the same snapshot that
     * publishes the source. No spare slot or second commit is needed. */
    if (existing>=0) {
        zero(v->staged_directory+(unsigned)existing*64,64);
        v->staged_slot=(unsigned)existing;
    }
    zero(e,32);unsigned i=0;while (next[i]) { e[i]=(unsigned char)next[i];++i; }
    if (v->version>=3) { put32(e+40,parent); }
    r=commit(v);v->busy=false;return r;
}
enum fs_result persist_rename(struct persist_volume *v,const char *source,const char *target)
{ return move_file(v,source,target,false); }
enum fs_result persist_publish(struct persist_volume *v,const char *source,const char *target)
{ return move_file(v,source,target,true); }

static enum fs_result extent_archive_pass(const struct persist_volume *v,const unsigned char *directory,
    const unsigned char *extents,persist_emit emit,void *context,unsigned char digest[32])
{
    struct sha256 sum;sha256_init(&sum);sha256_update(&sum,directory,v->directory_bytes);
    for (unsigned i=0;emit && i<v->directory_bytes;i+=512) {
        if (!emit(directory+i,512,context)) { return FS_IO; }
    }
    const unsigned char empty[512]={0};unsigned char sector[512],file_digest[32];
    for (unsigned slot=0;slot<v->files;++slot) {
        unsigned length=u32(directory+slot*64+32);
        const unsigned char *row=extents+slot*64;
        struct sha256 file;sha256_init(&file);
        for (unsigned offset=0;offset<v->file_bytes;offset+=512) {
            const unsigned char *data=empty;
            if (offset<length) {
                if (block_read(&v->device,(uint64_t)v->start+u32(row)+offset/512,sector)!=BLOCK_OK) { return FS_IO; }
                unsigned used=length-offset<512 ? length-offset : 512;
                if (!zeros(sector+used,512-used)) { return FS_CORRUPT; }
                sha256_update(&file,sector,used);data=sector;
            }
            sha256_update(&sum,data,512);
            if (emit && !emit(data,512,context)) { return FS_IO; }
        }
        sha256_final(&file,file_digest);
        if (length && !equal(file_digest,row+4,32)) { return FS_CORRUPT; }
    }
    sha256_final(&sum,digest);return FS_OK;
}
static enum fs_result export_extents(struct persist_volume *v,bool previous,persist_emit emit,void *context)
{
    const unsigned char *directory=v->live,*extents=v->extents;
    uint64_t generation=v->generation;
    struct extent_index other;
    unsigned char h[512]={0},digest[32];
    if (previous) {
        struct persist_bank_report report=scan_extents(v,v->bank^1,h,&other);
        if (report.state==PERSIST_BANK_UNREADABLE) { return FS_IO; }
        if (report.state==PERSIST_BANK_BAD_HEADER || report.state==PERSIST_BANK_BAD_PAYLOAD) { return FS_CORRUPT; }
        if (report.state!=PERSIST_BANK_VALID || report.generation>=v->generation) { return FS_NOT_FOUND; }
        directory=other.directory;extents=other.extents;generation=report.generation;zero(h,sizeof(h));
    }
    enum fs_result result=extent_archive_pass(v,directory,extents,NULL,NULL,h+32);
    if (result!=FS_OK) { return result; }
    magic(v,h);put32(h+8,5);put64(h+16,generation);put32(h+24,DATA_V5_ARCHIVE_BYTES-512);
    hash(h,64,h+64);
    if (!emit(h,sizeof(h),context)) { return FS_IO; }
    result=extent_archive_pass(v,directory,extents,emit,context,digest);
    return result!=FS_OK ? result : equal(digest,h+32,32) ? FS_OK : FS_CORRUPT;
}

enum fs_result persist_export(struct persist_volume *v,persist_emit emit,void *context)
{
    if (!v || !emit) { return FS_BAD_ARGUMENT; }if (!v->ready) { return FS_NOT_READY; }
    if (v->failed) { return FS_IO; }if (v->busy) { return FS_BUSY; }v->busy=true;
    if (v->version==5) { enum fs_result r=export_extents(v,false,emit,context);v->busy=false;return r; }
    unsigned char h[512]={0};magic(v,h);
    put32(h+8,v->version);put64(h+16,v->generation);put32(h+24,v->payload_bytes);
    hash(v->live,v->payload_bytes,h+32);hash(h,64,h+64);
    bool ok=emit(h,sizeof(h),context);
    for (unsigned i=0;ok && i<v->payload_bytes;i+=512) { ok=emit(v->live+i,512,context); }
    v->busy=false;return ok ? FS_OK : FS_IO;
}
enum fs_result persist_export_previous(struct persist_volume *v,persist_emit emit,void *context)
{
    if (!v || !emit) { return FS_BAD_ARGUMENT; }
    if (!v->ready) { return FS_NOT_READY; }
    if (v->busy) { return FS_BUSY; }
    v->busy=true;
    if (v->version==5) { enum fs_result r=export_extents(v,true,emit,context);v->busy=false;return r; }
    unsigned bank=v->bank^1;
    unsigned char header[512],sector[512],digest[32];
    struct persist_bank_report report=scan_bank(v,bank,header);
    enum fs_result result=FS_NOT_FOUND;
    if (report.state==PERSIST_BANK_UNREADABLE) { result=FS_IO; }
    else if (report.state==PERSIST_BANK_BAD_HEADER || report.state==PERSIST_BANK_BAD_PAYLOAD) { result=FS_CORRUPT; }
    if (report.state!=PERSIST_BANK_VALID || report.generation>=v->generation) { goto done; }

    /* The serial archive uses bank zero. Keep the verified payload digest
     * and check it again while sending the second read of the payload. */
    put32(header+12,0);
    hash(header,64,header+64);
    result=FS_IO;
    if (!emit(header,sizeof(header),context)) { goto done; }
    struct sha256 sum;
    sha256_init(&sum);
    uint64_t first=bank_start(v,bank)+1;
    for (unsigned offset=0;offset<v->payload_bytes;offset+=512) {
        if (block_read(&v->device,first+offset/512,sector)!=BLOCK_OK) { goto done; }
        sha256_update(&sum,sector,sizeof(sector));
        if (!emit(sector,sizeof(sector),context)) { goto done; }
    }
    sha256_final(&sum,digest);
    result=equal(digest,header+32,sizeof(digest)) ? FS_OK : FS_CORRUPT;
done:
    v->busy=false;
    return result;
}
enum fs_result persist_restore(struct persist_volume *v,size_t length,persist_input input,void *context)
{
    if (!v || !input) { return FS_BAD_ARGUMENT; }if (!v->ready) { return FS_NOT_READY; }
    if (v->version==5) { return FS_UNSUPPORTED; }
    if (v->failed) { return FS_IO; }if (v->busy) { return FS_BUSY; }
    for (unsigned i=0;i<v->files;++i) { if (v->live[i*64]) { return FS_BUSY; } }
    if (length!=512u+v->payload_bytes) { return FS_UNSUPPORTED; }
    v->busy=true;unsigned char h[512];enum fs_result r=FS_IO;
    if (!input(h,512,context)) { goto done; }
    if (!valid_header(v,h,0)) { r=FS_CORRUPT;goto done; }
    /* The required empty target has an all-zero payload. Reuse its live
     * buffer for validation while all public reads/mutations refuse reentry.
     * Any rejection/failure restores the empty RAM view before returning. */
    v->restoring=true;
    for (unsigned i=0;i<v->payload_bytes;i+=512) { if (!input(v->live+i,512,context)) { goto done; } }
    if (!checksum(v->live,v->payload_bytes,h+32) || !valid_payload(v,v->live)) { r=FS_CORRUPT;goto done; }
    stage_begin(v);r=commit(v);
done:
    if (r!=FS_OK && v->restoring) { zero(v->live,v->payload_bytes); }
    stage_clear(v);v->restoring=false;v->busy=false;return r;
}

/* Validate the full archive before touching the alternate bank, then read
 * the actual files again and check their identities before publication. */
static enum fs_result prepare_extent_restore(struct persist_volume *v,unsigned files,unsigned cap,
    persist_read_at read_at,void *context)
{
    unsigned char header[512],sector[512],digest[32];
    unsigned directory=files*64,version=files==DATA_V4_FILES ? 4 : 5;
    unsigned payload=directory+files*cap;
    if (!read_at(0,header,512,context) || !read_at(512,v->staged_directory,directory,context)) { return FS_IO; }
    if (!equal(header,"OUROSBK",7) || header[7]!='0'+version || u32(header+8)!=version || u32(header+12) ||
        !u64(header+16) || u32(header+24)!=payload || u32(header+28) || !zeros(header+96,416) ||
        !checksum(header,64,header+64) || !valid_directory(v,v->staged_directory)) { return FS_CORRUPT; }
    struct sha256 sum;sha256_init(&sum);sha256_update(&sum,v->staged_directory,directory);
    unsigned pool=DATA_V5_FIRST_EXTENT;
    for (unsigned slot=0;slot<files;++slot) {
        unsigned length=u32(v->staged_directory+slot*64+32);
        if (length>cap) { return FS_CORRUPT; }
        struct sha256 file;sha256_init(&file);
        for (unsigned offset=0;offset<cap;offset+=512) {
            if (!read_at(512+directory+slot*cap+offset,sector,512,context)) { return FS_IO; }
            unsigned used=offset<length ? length-offset : 0;if (used>512) { used=512; }
            if (!zeros(sector+used,512-used)) { return FS_CORRUPT; }
            sha256_update(&file,sector,used);sha256_update(&sum,sector,512);
        }
        if (length) {
            if (sectors_for(length)>v->sectors-pool) { return FS_NO_SPACE; }
            unsigned char *row=v->staged_extents+slot*64;
            put32(row,pool);sha256_final(&file,row+4);pool+=sectors_for(length);
        }
    }
    sha256_final(&sum,digest);return equal(digest,header+32,32) ? FS_OK : FS_CORRUPT;
}
enum fs_result persist_restore_extents(struct persist_volume *v,size_t length,persist_read_at read_at,void *context)
{
    if (!v || !read_at) { return FS_BAD_ARGUMENT; }
    if (!v->ready) { return FS_NOT_READY; }
    if (v->failed) { return FS_IO; }
    if (v->busy) { return FS_BUSY; }
    if (v->version!=5 || v->generation==UINT64_MAX) { return FS_UNSUPPORTED; }
    unsigned files,cap;
    if (length==512+DATA_V4_PAYLOAD_BYTES) { files=DATA_V4_FILES;cap=DATA_V4_FILE_BYTES; }
    else if (length==DATA_V5_ARCHIVE_BYTES) { files=DATA_V5_FILES;cap=DATA_V5_FILE_BYTES; }
    else { return FS_UNSUPPORTED; }
    for (unsigned i=0;i<v->files;++i) { if (v->live[i*64]) { return FS_BUSY; } }
    v->busy=true;stage_begin(v);
    enum fs_result result=prepare_extent_restore(v,files,cap,read_at,context);
    if (result!=FS_OK) { goto done; }
    if (!extent_begin(v)) { goto io; }
    for (unsigned i=0;i<files;++i) {
        unsigned bytes=u32(v->staged_directory+i*64+32);
        if (!bytes) { continue; }
        const unsigned char *row=v->staged_extents+i*64;
        unsigned padded=sectors_for(bytes)*512;
        v->write_phase="archive-file-read";
        if (!read_at(512+files*64+i*cap,v->staged_file,padded,context) ||
            !checksum(v->staged_file,bytes,row+4) || !zeros(v->staged_file+bytes,padded-bytes) ||
            !extent_write_file(v,u32(row),bytes)) { goto io; }
    }
    if (!extent_publish(v)) { goto io; }
    goto done;
io:
    v->failed=true;result=FS_IO;
done:
    stage_clear(v);v->busy=false;return result;
}
