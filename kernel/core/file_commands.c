#include "kfile.h"
#include "vfs.h"
#include "task.h"
#include "heap.h"
#include "memory.h"
#include "console.h"
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static void spaces(const char **p) { while (**p==' ') { ++*p; } }
static bool word(const char **p,char *out,size_t max)
{
    spaces(p);size_t n=0;while (**p && **p!=' ') { if (n+1>=max) { return false; }out[n++]=*(*p)++; }out[n]=0;return n!=0;
}
static bool number(const char **p,uint64_t *out)
{
    spaces(p);uint64_t value=0;bool any=false;
    while (**p>='0' && **p<='9') { unsigned d=(unsigned)(*(*p)++-'0');if (value>(UINT64_MAX-d)/10) { return false; }value=value*10+d;any=true; }
    if (!any || (**p && **p!=' ')) { return false; }*out=value;return true;
}
static bool signed_number(const char **p,int64_t *out)
{
    spaces(p);bool negative=**p=='-';if (negative || **p=='+') { ++*p;if (**p<'0' || **p>'9') { return false; } }uint64_t n;
    if (!number(p,&n) || n>(uint64_t)INT64_MAX+(negative ? 1u : 0u)) { return false; }
    *out=negative ? n==(1ULL<<63) ? INT64_MIN : -(int64_t)n : (int64_t)n;return true;
}
static bool end(const char *p) { spaces(&p);return !*p; }
static void report(enum fs_result r) { if (r!=FS_OK) { console_write("file: ");console_write(ramfs_error(r));console_write("\n"); } }
static void text_bytes(const unsigned char *p,size_t n)
{ for (size_t i=0;i<n;++i) { unsigned char c=p[i];console_putc((c>=32 && c<127) || c=='\r' || c=='\n' || c=='\t' ? (char)c : '.'); } }
static void selftest(void)
{
    const uint64_t owner=UINT64_MAX;const char *dir="/tmp/.filetest",*path="/tmp/.filetest/note";
    char absolute[RAMFS_PATH_MAX];bool directory;
    if (vfs_resolve("/",dir,absolute,&directory)!=FS_NOT_FOUND || kfile_count()>KFILE_LIMIT-3) {
        console_write("File test refused: scratch path exists or fewer than three free handles.\n");return;
    }
    struct heap_stats heap=heap_get_stats();struct page_stats pages=pages_stats();unsigned before=kfile_count();
    uint64_t w=0,a=0,b=0;size_t n=0;uint32_t at=0;unsigned char data[16];bool good=vfs_mkdir(dir)==FS_OK;
    if (!good) { console_write("FAIL: file-test scratch directory.\n");return; }
    good=kfile_open(owner,"/",path,KFILE_READ|KFILE_WRITE|KFILE_CREATE|KFILE_TRUNC,&w)==FS_OK && good;
    if (good) {
        good=kfile_write(owner,w,"abcdef",6,&n)==FS_OK && n==6;
        good=kfile_seek(owner,w,2,KFILE_SET,&at)==FS_OK && at==2 && good;
        good=kfile_write(owner,w,"XY",2,&n)==FS_OK && n==2 && good;
        good=kfile_seek(owner,w,0,KFILE_SET,&at)==FS_OK && good;
        good=kfile_read(owner,w,data,6,&n)==FS_OK && n==6 && data[0]=='a' && data[2]=='X' && data[5]=='f' && good;
        good=kfile_read(1,w,data,1,&n)==FS_BAD_HANDLE && n==0 && good;
        good=vfs_write("/tmp//.filetest/./note","bad",3)==FS_BUSY && vfs_remove(dir,true)==FS_BUSY && good;
        good=kfile_close(owner,w)==FS_OK && good;
        good=kfile_read(owner,w,data,1,&n)==FS_BAD_HANDLE && good;
        good=kfile_open(owner,"/",path,KFILE_READ,&a)==FS_OK && good;
        good=kfile_open(owner,"/",path,KFILE_READ,&b)==FS_OK && good;
        good=kfile_read(owner,a,data,2,&n)==FS_OK && n==2 && data[0]=='a' && good;
        good=kfile_read(owner,b,data,1,&n)==FS_OK && n==1 && data[0]=='a' && good;
        good=kfile_seek(owner,b,-1,KFILE_END,&at)==FS_OK && at==5 && good;
        good=kfile_seek(owner,b,1,KFILE_END,&at)==FS_BAD_ARGUMENT && good;
    }
    unsigned closed=kfile_close_owner(owner);if (closed!=2) { good=false; }
    if (vfs_remove(path,false)!=FS_OK || vfs_remove(dir,true)!=FS_OK) { good=false; }
    struct heap_stats final=heap_get_stats();struct page_stats after=pages_stats();
    if (kfile_count()!=before || !heap_check() || heap.pages!=final.pages || heap.objects!=final.objects || heap.capacity_bytes!=final.capacity_bytes ||
        pages.allocated_pages!=after.allocated_pages || pages.free_pages!=after.free_pages) { good=false; }
    console_write(good ? "PASS: owned file handles, independent cursors, seek/overwrite, stale/foreign rejection, path pins and owner cleanup; memory restored.\n" : "FAIL: owned file-handle test.\n");
}
bool file_command(const char *line)
{
    const char *p=line;char verb[16];if (!word(&p,verb,sizeof(verb))) { return false; }
    if (same(verb,"fileinfo") || same(verb,"filetest")) {
        if (!end(p)) { console_write("Usage: fileinfo | filetest\n");return true; }
        if (same(verb,"fileinfo")) { kfile_info(); }else { selftest(); }return true;
    }
    if (!same(verb,"fopen") && !same(verb,"fread") && !same(verb,"fwrite") && !same(verb,"fseek") && !same(verb,"fclose")) { return false; }
    uint64_t owner=task_file_owner(),handle=0;enum fs_result r;
    if (same(verb,"fopen")) {
        char path[RAMFS_PATH_MAX],mode[8];
        if (!word(&p,path,sizeof(path)) || !word(&p,mode,sizeof(mode)) || !end(p)) { goto usage; }
        unsigned flags=same(mode,"r") ? KFILE_READ : same(mode,"rw") ? KFILE_READ|KFILE_WRITE : same(mode,"w") ? KFILE_WRITE|KFILE_CREATE|KFILE_TRUNC : 0;
        if (!flags) { goto usage; }
        /* The monitor supplies its actual cwd through filesystem_cwd(). */
        r=kfile_open(owner,filesystem_cwd(),path,flags,&handle);report(r);
        if (r==FS_OK) { console_write("Opened handle=");console_u64(handle);console_write("\n"); }return true;
    }
    if (!number(&p,&handle)) { goto usage; }
    if (same(verb,"fclose")) {
        if (!end(p)) { goto usage; }r=kfile_close(owner,handle);report(r);if (r==FS_OK) { console_write("File closed.\n"); }return true;
    }
    if (same(verb,"fread")) {
        uint64_t count;if (!number(&p,&count) || !end(p) || !count || count>512) { goto usage; }
        unsigned char data[512];size_t n;r=kfile_read(owner,handle,data,(size_t)count,&n);report(r);
        if (r==FS_OK) { text_bytes(data,n);if (n && data[n-1]!='\n') { console_write("\n"); }console_write("Read bytes=");console_u64(n);console_write("\n"); }return true;
    }
    if (same(verb,"fwrite")) {
        spaces(&p);size_t n=0;while (p[n]) { ++n; }size_t written;r=kfile_write(owner,handle,p,n,&written);report(r);
        if (r==FS_OK) { console_write("Written bytes=");console_u64(written);console_write("\n"); }return true;
    }
    int64_t delta;char base[8];if (!signed_number(&p,&delta) || !word(&p,base,sizeof(base)) || !end(p)) { goto usage; }
    enum kfile_whence whence=same(base,"set") ? KFILE_SET : same(base,"cur") ? KFILE_CUR : same(base,"end") ? KFILE_END : (enum kfile_whence)99;
    if (whence==(enum kfile_whence)99) { goto usage; }uint32_t position;r=kfile_seek(owner,handle,delta,whence,&position);report(r);
    if (r==FS_OK) { console_write("File offset=");console_u64(position);console_write("\n"); }return true;
usage:
    console_write("Usage: fopen <path> r|rw|w | fread <handle> <1..512> | fwrite <handle> <text>\nfseek <handle> <signed offset> set|cur|end | fclose <handle>\n");return true;
}
