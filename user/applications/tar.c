#include "dir_walk.h"
#include "elf_app.h"
#include "text_io.h"
#include "ustar.h"

enum operation { CREATE, LIST, VERIFY, EXTRACT, COMPARE };
enum file_result { FILE_MATCH, FILE_DIFFERENT, FILE_ERROR };

struct tar_archive {
    unsigned char *data;
    struct ustar_index index;
};

static bool same(const char *a,const char *b)
{
    while (*a && *a==*b) { ++a;++b; }
    return *a==*b;
}

/* The first walk entry is the source directory itself. Subsequent paths
 * have the same prefix even when the caller supplied "." or "/". */
static const char *member_name(size_t *prefix,const char *path)
{
    if (*prefix) { return path+*prefix; }
    size_t n=uos_length(path);
    *prefix=n+(path[n-1]!='/');
    return NULL;
}

static enum file_result transfer_file(const char *path,unsigned char *out,const unsigned char *expected,size_t bytes)
{
    int64_t file=uos_open(path,UOS_READ);
    if (file<0) { return FILE_ERROR; }
    unsigned char chunk[TEXT_IO_BYTES];
    enum file_result result=FILE_MATCH;
    size_t at=0;
    while (at<bytes) {
        size_t count=bytes-at;
        if (count>sizeof(chunk)) { count=sizeof(chunk); }
        unsigned char *buffer=out ? out+at : chunk;
        int64_t n=uos_read((uint64_t)file,buffer,count);
        if (n<0) { result=FILE_ERROR;break; }
        if (!n || (expected && text_compare(buffer,(size_t)n,expected+at,(size_t)n))) {
            result=FILE_DIFFERENT;
            break;
        }
        at+=(size_t)n;
    }
    if (result==FILE_MATCH) {
        int64_t extra=uos_read((uint64_t)file,chunk,1);
        if (extra) { result=extra<0 ? FILE_ERROR : FILE_DIFFERENT; }
    }
    return uos_close((uint64_t)file) ? FILE_ERROR : result;
}

struct create_walk {
    unsigned char *data;
    size_t used,prefix;
};

static bool add_member(const char *path,const char *name,const struct uos_entry *entry,void *context)
{
    (void)name;
    struct create_walk *walk=context;
    const char *relative=member_name(&walk->prefix,path);
    bool directory=entry->type==UOS_DIRECTORY;
    if (!relative) { return directory; }
    uint64_t bytes=directory ? 0 : entry->bytes;
    if (bytes>USTAR_LIMIT) { return false; }
    size_t padded=((size_t)bytes+USTAR_BLOCK-1)&~(size_t)(USTAR_BLOCK-1);
    if (USTAR_BLOCK+padded>USTAR_LIMIT-2*USTAR_BLOCK-walk->used) { return false; }
    unsigned char *header=walk->data+walk->used;
    if (!ustar_make_header(header,relative,(uint32_t)bytes,directory)) { return false; }
    unsigned char *payload=header+USTAR_BLOCK;
    if (!directory && transfer_file(path,payload,NULL,(size_t)bytes)!=FILE_MATCH) { return false; }
    for (size_t i=(size_t)bytes;i<padded;++i) { payload[i]=0; }
    walk->used+=USTAR_BLOCK+padded;
    return true;
}

static int create_archive(char root[USTAR_PATH],struct tar_archive *archive)
{
    struct create_walk walk={.data=archive->data};
    if (dir_walk(root,add_member,&walk)!=DIR_WALK_OK) { return 2; }
    size_t bytes=walk.used+2*USTAR_BLOCK;
    for (size_t i=walk.used;i<bytes;++i) { archive->data[i]=0; }
    return ustar_scan(archive->data,bytes,&archive->index) && text_write(archive->data,bytes) ? 0 : 2;
}

static bool read_archive(const char *path,struct tar_archive *archive)
{
    int64_t input=file_input_open(path);
    if (input<0) { return false; }
    size_t used=0;
    int64_t n=0;
    while (used<USTAR_LIMIT) {
        size_t count=USTAR_LIMIT-used;
        if (count>TEXT_IO_BYTES) { count=TEXT_IO_BYTES; }
        n=file_input_read(input,archive->data+used,count);
        if (n<=0) { break; }
        used+=(size_t)n;
    }
    if (used==USTAR_LIMIT) {
        unsigned char extra;
        n=file_input_read(input,&extra,1);
    }
    return !file_input_finish(input,n==0 ? 0 : 2) && ustar_scan(archive->data,used,&archive->index);
}

static unsigned find_member(const struct tar_archive *archive,const char *name,struct ustar_member *member)
{
    for (unsigned i=0;i<archive->index.count;++i) {
        ustar_member_at(archive->data,archive->index.offsets[i],member);
        if (same(name,member->path)) { return i; }
    }
    return archive->index.count;
}

static const unsigned char *member_data(const struct tar_archive *archive,unsigned index)
{
    return archive->data+archive->index.offsets[index]+USTAR_BLOCK;
}

static int list_archive(const struct tar_archive *archive)
{
    for (unsigned i=0;i<archive->index.count;++i) {
        struct ustar_member member;
        ustar_member_at(archive->data,archive->index.offsets[i],&member);
        char field[11];
        unsigned start=sizeof(field)-1;
        field[start]=' ';
        uint32_t bytes=member.bytes;
        do { field[--start]=(char)('0'+bytes%10);bytes/=10; } while (bytes);
        if (!text_write(member.directory ? "dir " : "file ",member.directory ? 4 : 5) ||
            !text_write(field+start,sizeof(field)-start) ||
            !text_write(member.path,uos_length(member.path)) || !text_write("\n",1)) { return 2; }
    }
    return 0;
}

struct compare_walk {
    const struct tar_archive *archive;
    size_t prefix;
    uint64_t seen;
    int result;
};
_Static_assert(USTAR_MEMBERS<=64,"archive comparison seen mask");

static bool mismatch(struct compare_walk *walk,const char *path)
{
    uos_puts("tar: differs at ");
    uos_puts(path);
    uos_puts("\n");
    walk->result=1;
    return false;
}

static bool compare_member(const char *path,const char *name,const struct uos_entry *entry,void *context)
{
    (void)name;
    struct compare_walk *walk=context;
    const char *relative=member_name(&walk->prefix,path);
    bool directory=entry->type==UOS_DIRECTORY;
    if (!relative) { return directory; }
    struct ustar_member member;
    unsigned index=find_member(walk->archive,relative,&member);
    if (index==walk->archive->index.count || member.directory!=directory ||
        (!directory && member.bytes!=entry->bytes)) { return mismatch(walk,relative); }
    if (!directory) {
        enum file_result result=transfer_file(path,NULL,member_data(walk->archive,index),member.bytes);
        if (result==FILE_DIFFERENT) { return mismatch(walk,relative); }
        if (result==FILE_ERROR) { return false; }
    }
    walk->seen|=(uint64_t)1<<index;
    return true;
}

static int compare_archive(char root[USTAR_PATH],const struct tar_archive *archive)
{
    struct compare_walk walk={.archive=archive};
    if (dir_walk(root,compare_member,&walk)!=DIR_WALK_OK) {
        if (!walk.result) { uos_puts("tar: cannot read directory tree\n"); }
        return walk.result ? walk.result : 2;
    }
    for (unsigned i=0;i<archive->index.count;++i) {
        if (!(walk.seen&((uint64_t)1<<i))) {
            struct ustar_member member;
            ustar_member_at(archive->data,archive->index.offsets[i],&member);
            (void)mismatch(&walk,member.path);
            return walk.result;
        }
    }
    return 0;
}

static int archive_operation(enum operation op,char path[USTAR_PATH],char target[USTAR_PATH],struct tar_archive *archive)
{
    if (op==CREATE) { return create_archive(path,archive); }
    if (!read_archive(path,archive)) {
        if (op!=EXTRACT) { uos_puts("tar: invalid, unsupported or unreadable archive\n"); }
        return 2;
    }
    if (op==LIST) { return list_archive(archive); }
    if (op==VERIFY) {
        const char valid[]="tar: valid ustar archive\n";
        return text_write(valid,sizeof(valid)-1) ? 0 : 2;
    }
    if (op==COMPARE) { return compare_archive(target,archive); }
    struct ustar_member member;
    unsigned index=find_member(archive,target,&member);
    if (index==archive->index.count || member.directory) { return 1; }
    return text_write(member_data(archive,index),member.bytes) ? 0 : 2;
}

int app_main(void)
{
    static const char *const names[]={"create","list","verify","extract","compare"};
    int64_t argc=uos_argc();
    char mode[8],path[USTAR_PATH],target[USTAR_PATH];
    if (argc<3 || argc>4 || uos_arg(1,mode,sizeof(mode))<=0 ||
        uos_arg(2,path,sizeof(path))<=0) { goto usage; }
    unsigned op=0;
    for (;op<sizeof(names)/sizeof(names[0]);++op) { if (same(names[op],mode)) { break; } }
    if (op>COMPARE || argc!=(op>=EXTRACT ? 4 : 3) ||
        (argc==4 && uos_arg(3,target,sizeof(target))<=0)) { goto usage; }
    int64_t storage=uos_alloc_pages(USTAR_LIMIT/APP_PAGE_BYTES);
    if (storage<=0) { return 2; }
    struct tar_archive archive={.data=(void *)(uintptr_t)storage};
    int result=archive_operation((enum operation)op,path,target,&archive);
    return uos_free_pages(archive.data) ? 2 : result;
usage:
    uos_puts("Usage: tar create DIR > NEW.tar | tar list|verify ARCHIVE\n"
             "       tar extract ARCHIVE MEMBER > NEWFILE | tar compare ARCHIVE DIR\n"
             "       Use - for archive input from stdin.\n");
    return 2;
}
