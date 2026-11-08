/* Original read-only FAT32 subset: MBR FAT32 partitions, 512-byte sectors,
 * mirrored FATs, short names and bounded chains. Long filenames are skipped. */
#include "fat32.h"
static uint16_t u16(const unsigned char *p) { return (uint16_t)p[0] | (uint16_t)p[1]<<8; }
static uint32_t u32(const unsigned char *p) { return (uint32_t)u16(p) | (uint32_t)u16(p+2)<<16; }
static bool valid_cluster(const struct fat32_volume *v,uint32_t c) { return c>=2 && c<v->clusters+2; }
static enum fs_result sector(struct fat32_volume *v,uint32_t lba,unsigned char *data)
{
    if (lba<v->start || (uint64_t)lba-v->start>=v->sectors) { return FS_CORRUPT; }
    return block_read(&v->device,lba,data)==BLOCK_OK ? FS_OK : FS_IO;
}
enum fs_result fat32_mount(struct fat32_volume *v,const struct block_device *device)
{
    if (!v || !device) { return FS_BAD_ARGUMENT; }
    *v=(struct fat32_volume){0};
    unsigned char mbr[512],b[512];
    if (block_read(device,0,mbr)!=BLOCK_OK) { return FS_IO; }
    if (mbr[510]!=0x55 || mbr[511]!=0xaa) { return FS_UNSUPPORTED; }
    unsigned matches=0; uint32_t start=0,total=0;
    for (unsigned i=0;i<4;++i) {
        const unsigned char *p=mbr+446+i*16;
        if (p[4]==0x0b || p[4]==0x0c) { ++matches; start=u32(p+8); total=u32(p+12); }
    }
    if (matches!=1) { return FS_UNSUPPORTED; }
    if (!start || !total || (uint64_t)start+total>device->sectors || (uint64_t)start+total>UINT32_MAX) { return FS_CORRUPT; }
    if (block_read(device,start,b)!=BLOCK_OK) { return FS_IO; }
    unsigned spc=b[13],fats=b[16]; uint32_t reserved=u16(b+14),fat=u32(b+36),sectors=u32(b+32);
    uint64_t overhead=(uint64_t)reserved+(uint64_t)fats*fat;
    if (b[510]!=0x55 || b[511]!=0xaa || u16(b+11)!=512 || !spc || spc>128 || (spc&(spc-1)) ||
        !reserved || (fats!=1 && fats!=2) || !fat || !sectors || sectors>total || overhead>=sectors ||
        u16(b+17) || u16(b+19) || u16(b+22) || u16(b+42) || (u16(b+40)&0x80)) { return FS_UNSUPPORTED; }
    uint32_t clusters=(sectors-(uint32_t)overhead)/spc,root=u32(b+44);
    if (clusters<65525 || clusters>0x0fffffed || (uint64_t)(clusters+2)*4>(uint64_t)fat*512 ||
        root<2 || root>=clusters+2) { return FS_CORRUPT; }
    *v=(struct fat32_volume){.device=*device,.start=start,.sectors=sectors,.fat_lba=start+reserved,
        .fat_sectors=fat,.data_lba=start+(uint32_t)overhead,.clusters=clusters,.root=root,.spc=spc,.fats=fats,.ready=true};
    return FS_OK;
}
static enum fs_result next_cluster(struct fat32_volume *v,uint32_t current,uint32_t *next)
{
    if (!valid_cluster(v,current)) { return FS_CORRUPT; }
    unsigned char data[512],other[512]; uint32_t lba=v->fat_lba+current/128;
    enum fs_result r=sector(v,lba,data); if (r!=FS_OK) { return r; }
    if (v->fats==2) {
        r=sector(v,lba+v->fat_sectors,other); if (r!=FS_OK) { return r; }
        for (unsigned i=0;i<512;++i) { if (data[i]!=other[i]) { return FS_CORRUPT; } }
    }
    *next=u32(data+(current%128)*4)&0x0fffffff;
    if (*next>=0x0ffffff8) { return FS_OK; }
    return valid_cluster(v,*next) ? FS_OK : FS_CORRUPT;
}
static bool equal_case(const char *a,const char *b)
{
    for (;;) {
        unsigned char x=(unsigned char)*a++,y=(unsigned char)*b++;
        if (x>='a' && x<='z') { x-=32; } if (y>='a' && y<='z') { y-=32; }
        if (x!=y) { return false; } if (!x) { return true; }
    }
}
static enum fs_result directory(struct fat32_volume *v,uint32_t cluster,fat32_emit emit,void *context)
{
    if (!valid_cluster(v,cluster)) { return FS_CORRUPT; }
    for (unsigned chain=0;chain<4096 && chain<v->clusters;++chain) {
        unsigned char data[512];
        for (unsigned s=0;s<v->spc;++s) {
            enum fs_result r=sector(v,v->data_lba+(cluster-2)*v->spc+s,data); if (r!=FS_OK) { return r; }
            for (unsigned i=0;i<512;i+=32) {
                const unsigned char *p=data+i;
                if (!p[0]) { return FS_OK; }
                if (p[0]==0xe5 || p[11]==0x0f || (p[11]&8) || p[0]=='.') { continue; }
                if (p[11]&0xc0) { return FS_CORRUPT; }
                struct fat32_entry e={.cluster=((uint32_t)u16(p+20)<<16)|u16(p+26),.size=u32(p+28),.directory=(p[11]&16)!=0};
                unsigned n=0,last=8;
                while (last && p[last-1]==' ') { --last; }
                if (!last) { return FS_CORRUPT; }
                for (unsigned j=0;j<last;++j) {
                    if (p[j]<33 || p[j]>126 || p[j]=='/' || p[j]=='\\') { return FS_UNSUPPORTED; }
                    e.name[n++]=(char)p[j];
                }
                last=3; while (last && p[8+last-1]==' ') { --last; }
                if (last) {
                    e.name[n++]='.';
                    for (unsigned j=0;j<last;++j) {
                        if (p[8+j]<33 || p[8+j]>126 || p[8+j]=='/' || p[8+j]=='\\') { return FS_UNSUPPORTED; }
                        e.name[n++]=(char)p[8+j];
                    }
                }
                e.name[n]='\0';
                if ((e.directory || e.size) && !valid_cluster(v,e.cluster)) { return FS_CORRUPT; }
                if (!emit(&e,context)) { return FS_OK; }
            }
        }
        uint32_t next; enum fs_result r=next_cluster(v,cluster,&next); if (r!=FS_OK) { return r; }
        if (next>=0x0ffffff8) { return FS_OK; } cluster=next;
    }
    return FS_CORRUPT;
}
struct search { const char *name; struct fat32_entry entry; bool found; };
static bool find_entry(const struct fat32_entry *entry,void *context)
{
    struct search *s=context;
    if (!equal_case(entry->name,s->name)) { return true; }
    s->entry=*entry; s->found=true; return false;
}
enum fs_result fat32_lookup(struct fat32_volume *v,const char *path,struct fat32_entry *entry)
{
    if (!v || !v->ready) { return FS_NOT_READY; }
    if (!path || !entry) { return FS_BAD_ARGUMENT; }
    size_t length=0; while (length<RAMFS_PATH_MAX && path[length]) { ++length; }
    if (length==RAMFS_PATH_MAX) { return FS_PATH_TOO_LONG; }
    if (!length || *path!='/') { return FS_INVALID_PATH; }
    struct fat32_entry current={.directory=true,.cluster=v->root};
    const char *p=path;
    while (*p) {
        if (!current.directory) { return FS_NOT_DIR; }
        while (*p=='/') { ++p; } if (!*p) { break; }
        char name[13]; unsigned n=0;
        while (*p && *p!='/') { if (n>=12) { return FS_NAME_TOO_LONG; } name[n++]=*p++; }
        name[n]='\0';
        if (equal_case(name,".") || equal_case(name,"..")) { return FS_INVALID_PATH; }
        struct search s={.name=name}; enum fs_result r=directory(v,current.cluster,find_entry,&s);
        if (r!=FS_OK) { return r; } if (!s.found) { return FS_NOT_FOUND; } current=s.entry;
    }
    *entry=current; return FS_OK;
}
enum fs_result fat32_list(struct fat32_volume *v,const char *path,fat32_emit emit,void *context)
{
    if (!emit) { return FS_BAD_ARGUMENT; }
    struct fat32_entry e; enum fs_result r=fat32_lookup(v,path,&e); if (r!=FS_OK) { return r; }
    if (!e.directory) { (void)emit(&e,context); return FS_OK; }
    return directory(v,e.cluster,emit,context);
}
enum fs_result fat32_read(struct fat32_volume *v,const struct fat32_entry *e,uint32_t offset,
                         void *buffer,size_t capacity,size_t *count)
{
    if (!v || !v->ready) { return FS_NOT_READY; }
    if (!e || !count || (!buffer && capacity)) { return FS_BAD_ARGUMENT; }
    *count=0; if (e->directory) { return FS_IS_DIR; }
    if (offset>=e->size || !capacity) { return FS_OK; }
    if (!valid_cluster(v,e->cluster)) { return FS_CORRUPT; }
    if (capacity>e->size-offset) { capacity=e->size-offset; }
    uint32_t cluster=e->cluster,cluster_bytes=v->spc*512,skip=offset/cluster_bytes;
    uint64_t needed=((uint64_t)e->size+cluster_bytes-1)/cluster_bytes;
    if (needed>4096 || needed>v->clusters) { return FS_FILE_TOO_LARGE; }
    uint32_t check=cluster;
    for (uint64_t i=0;i<needed;++i) {
        uint32_t next; enum fs_result r=next_cluster(v,check,&next); if (r!=FS_OK) { return r; }
        if (i+1==needed) { if (next<0x0ffffff8) { return FS_CORRUPT; } }
        else { if (next>=0x0ffffff8) { return FS_CORRUPT; } check=next; }
    }
    /* Bound random-offset traversal; streamed commands also cap total bytes. */
    if (skip>=4096) { return FS_FILE_TOO_LARGE; }
    for (uint32_t i=0;i<skip;++i) {
        uint32_t next; enum fs_result r=next_cluster(v,cluster,&next); if (r!=FS_OK) { return r; }
        if (next>=0x0ffffff8) { return FS_CORRUPT; } cluster=next;
    }
    uint32_t within=offset%cluster_bytes; unsigned chain=0; unsigned char *out=buffer;
    while (*count<capacity) {
        unsigned char data[512];
        enum fs_result r=sector(v,v->data_lba+(cluster-2)*v->spc+within/512,data); if (r!=FS_OK) { return r; }
        size_t n=512-within%512; if (n>capacity-*count) { n=capacity-*count; }
        for (size_t i=0;i<n;++i) { out[*count+i]=data[within%512+i]; }
        *count+=n; within+=(uint32_t)n;
        if (*count<capacity && within==cluster_bytes) {
            uint32_t next; r=next_cluster(v,cluster,&next); if (r!=FS_OK) { return r; }
            if (next>=0x0ffffff8 || ++chain>=4096) { return FS_CORRUPT; } cluster=next; within=0;
        }
    }
    return FS_OK;
}
