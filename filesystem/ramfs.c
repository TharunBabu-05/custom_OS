/* Bounded single-core RAM filesystem. Metadata is static; nonempty files own
 * one physical page each. There is no disk, mount table, permissions or heap. */
#include "ramfs.h"
#include "memory.h"
struct node {
    bool used, directory, has_page;
    int parent;
    char name[RAMFS_NAME_MAX+1];
    uint64_t page;
    size_t length;
};
static struct node nodes[RAMFS_NODES];
static bool ready;
static bool equal(const char *a,const char *b)
{ while (*a && *a==*b) { ++a; ++b; } return *a==*b; }
static bool valid_node(int n) { return n>=0 && n<RAMFS_NODES && nodes[n].used; }
static bool name_char(char c)
{ return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='.' || c=='_' || c=='-'; }
static int child(int parent,const char *name)
{
    for (int i=1;i<RAMFS_NODES;++i) {
        if (nodes[i].used && nodes[i].parent==parent && equal(nodes[i].name,name)) { return i; }
    }
    return -1;
}
static int free_node(void)
{ for (int i=1;i<RAMFS_NODES;++i) { if (!nodes[i].used) { return i; } } return -1; }
static void make_node(int n,int parent,const char *name,bool directory)
{
    nodes[n]=(struct node){.used=true,.directory=directory,.parent=parent};
    size_t i=0; do { nodes[n].name[i]=name[i]; } while (name[i++]);
}
static enum fs_result path_length(const char *path,size_t *length)
{
    if (!path) { return FS_BAD_ARGUMENT; }
    size_t n=0;
    while (n<RAMFS_PATH_MAX && path[n]) { ++n; }
    if (n==RAMFS_PATH_MAX) { return FS_PATH_TOO_LONG; }
    if (!n) { return FS_INVALID_PATH; }
    *length=n; return FS_OK;
}
enum fs_result ramfs_lookup(int cwd,const char *path,int *node)
{
    if (!ready) { return FS_NOT_READY; }
    if (!node || !valid_node(cwd) || !nodes[cwd].directory) { return FS_BAD_ARGUMENT; }
    size_t length; enum fs_result result=path_length(path,&length);
    if (result!=FS_OK) { return result; }
    (void)length;
    int current=*path=='/' ? 0 : cwd;
    const char *p=path;
    while (*p) {
        if (!nodes[current].directory) { return FS_NOT_DIR; }
        while (*p=='/') { ++p; }
        if (!*p) { break; }
        char name[RAMFS_NAME_MAX+1]; size_t n=0;
        while (*p && *p!='/') {
            if (!name_char(*p)) { return FS_INVALID_PATH; }
            if (n==RAMFS_NAME_MAX) { return FS_NAME_TOO_LONG; }
            name[n++]=*p++;
        }
        name[n]='\0';
        if (equal(name,".")) { continue; }
        if (equal(name,"..")) { current=nodes[current].parent; continue; }
        current=child(current,name);
        if (current<0) { return FS_NOT_FOUND; }
    }
    *node=current; return FS_OK;
}
enum fs_result ramfs_pwd(int node,char *buffer,size_t capacity)
{
    if (!ready) { return FS_NOT_READY; }
    if (!buffer || !capacity || !valid_node(node) || !nodes[node].directory) { return FS_BAD_ARGUMENT; }
    char reverse[RAMFS_PATH_MAX]; size_t used=0; unsigned depth=0;
    while (node) {
        if (++depth>=RAMFS_NODES || !valid_node(node)) { return FS_BAD_ARGUMENT; }
        size_t length=0; while (nodes[node].name[length]) { ++length; }
        if (used+length+1>=sizeof(reverse)) { return FS_PATH_TOO_LONG; }
        while (length) { reverse[used++]=nodes[node].name[--length]; }
        reverse[used++]='/'; node=nodes[node].parent;
    }
    if (!used) { reverse[used++]='/'; }
    if (capacity<=used) { return FS_PATH_TOO_LONG; }
    for (size_t i=0;i<used;++i) { buffer[i]=reverse[used-i-1]; }
    buffer[used]='\0'; return FS_OK;
}
static enum fs_result parent_path(int cwd,const char *path,bool directory,int *parent,char *name)
{
    size_t length; enum fs_result r=path_length(path,&length);
    if (r!=FS_OK) { return r; }
    if (path[length-1]=='/' && !directory) { return FS_NOT_DIR; }
    while (length && path[length-1]=='/') { --length; }
    if (!length) { return FS_EXISTS; }
    size_t start=length;
    while (start && path[start-1]!='/') { --start; }
    size_t name_length=length-start;
    if (name_length>RAMFS_NAME_MAX) { return FS_NAME_TOO_LONG; }
    for (size_t i=0;i<name_length;++i) {
        if (!name_char(path[start+i])) { return FS_INVALID_PATH; }
        name[i]=path[start+i];
    }
    name[name_length]='\0';
    if (equal(name,".") || equal(name,"..")) { return FS_INVALID_PATH; }
    if (start) {
        char prefix[RAMFS_PATH_MAX];
        for (size_t i=0;i<start;++i) { prefix[i]=path[i]; } prefix[start]='\0';
        r=ramfs_lookup(cwd,prefix,parent);
        if (r!=FS_OK) { return r; }
    } else { *parent=cwd; }
    if (!valid_node(*parent) || !nodes[*parent].directory) { return FS_NOT_DIR; }
    char canonical[RAMFS_PATH_MAX]; r=ramfs_pwd(*parent,canonical,sizeof(canonical));
    if (r!=FS_OK) { return r; }
    size_t n=0; while (canonical[n]) { ++n; }
    if (n+(*parent ? 1 : 0)+name_length>=RAMFS_PATH_MAX) { return FS_PATH_TOO_LONG; }
    return FS_OK;
}
enum fs_result ramfs_chdir(int cwd,const char *path,int *directory)
{
    if (!directory) { return FS_BAD_ARGUMENT; }
    int node; enum fs_result r=ramfs_lookup(cwd,path,&node);
    if (r!=FS_OK) { return r; }
    if (!nodes[node].directory) { return FS_NOT_DIR; }
    *directory=node; return FS_OK;
}
enum fs_result ramfs_list(int cwd,const char *path,ramfs_emit emit,void *context)
{
    if (!emit) { return FS_BAD_ARGUMENT; }
    int node; enum fs_result r=ramfs_lookup(cwd,path,&node);
    if (r!=FS_OK) { return r; }
    if (!nodes[node].directory) { emit(nodes[node].name,false,nodes[node].length,context); return FS_OK; }
    for (int i=1;i<RAMFS_NODES;++i) {
        if (nodes[i].used && nodes[i].parent==node) { emit(nodes[i].name,nodes[i].directory,nodes[i].length,context); }
    }
    return FS_OK;
}
enum fs_result ramfs_read(int cwd,const char *path,const char **data,size_t *length)
{
    if (!data || !length) { return FS_BAD_ARGUMENT; }
    int node; enum fs_result r=ramfs_lookup(cwd,path,&node);
    if (r!=FS_OK) { return r; }
    if (nodes[node].directory) { return FS_IS_DIR; }
    *data=nodes[node].has_page ? (const char *)(uintptr_t)nodes[node].page : "";
    *length=nodes[node].length; return FS_OK;
}
enum fs_result ramfs_mkdir(int cwd,const char *path)
{
    if (!ready) { return FS_NOT_READY; }
    if (!valid_node(cwd) || !nodes[cwd].directory) { return FS_BAD_ARGUMENT; }
    int parent; char name[RAMFS_NAME_MAX+1]; enum fs_result r=parent_path(cwd,path,true,&parent,name);
    if (r!=FS_OK) { return r; }
    if (child(parent,name)>=0) { return FS_EXISTS; }
    int node=free_node(); if (node<0) { return FS_NO_NODES; }
    make_node(node,parent,name,true); return FS_OK;
}
enum fs_result ramfs_write(int cwd,const char *path,const char *data,size_t length)
{
    if (!ready) { return FS_NOT_READY; }
    if ((!data && length) || !valid_node(cwd) || !nodes[cwd].directory) { return FS_BAD_ARGUMENT; }
    if (length>RAMFS_FILE_MAX) { return FS_FILE_TOO_LARGE; }
    int node; enum fs_result r=ramfs_lookup(cwd,path,&node);
    int parent=0; char name[RAMFS_NAME_MAX+1]={0}; bool create=false;
    if (r==FS_NOT_FOUND) {
        r=parent_path(cwd,path,false,&parent,name); if (r!=FS_OK) { return r; }
        node=free_node(); if (node<0) { return FS_NO_NODES; } create=true;
    } else if (r!=FS_OK) { return r;
    } else if (nodes[node].directory) { return FS_IS_DIR; }
    uint64_t page=0;
    if (length) {
        if (!create && nodes[node].has_page) { page=nodes[node].page; }
        else { if (!page_alloc(&page)) { return FS_NO_MEMORY; } }
    }
    if (!length && !create && nodes[node].has_page && !page_free(nodes[node].page)) { return FS_BAD_ARGUMENT; }
    if (create) { make_node(node,parent,name,false); }
    if (length) {
        char *destination=(char *)(uintptr_t)page;
        for (size_t i=0;i<length;++i) { destination[i]=data[i]; }
        nodes[node].page=page; nodes[node].has_page=true;
    } else { nodes[node].page=0; nodes[node].has_page=false; }
    nodes[node].length=length;
    return FS_OK;
}
enum fs_result ramfs_touch(int cwd,const char *path)
{
    int node; enum fs_result r=ramfs_lookup(cwd,path,&node);
    if (r==FS_OK) { return nodes[node].directory ? FS_IS_DIR : FS_OK; }
    if (r!=FS_NOT_FOUND) { return r; }
    return ramfs_write(cwd,path,NULL,0);
}
enum fs_result ramfs_remove(int cwd,const char *path,bool directory)
{
    int node; enum fs_result r=ramfs_lookup(cwd,path,&node);
    if (r!=FS_OK) { return r; }
    if (!node || node==cwd) { return FS_BUSY; }
    if (directory && !nodes[node].directory) { return FS_NOT_DIR; }
    if (!directory && nodes[node].directory) { return FS_IS_DIR; }
    if (directory) {
        for (int i=1;i<RAMFS_NODES;++i) { if (nodes[i].used && nodes[i].parent==node) { return FS_NOT_EMPTY; } }
    }
    if (nodes[node].has_page && !page_free(nodes[node].page)) { return FS_BAD_ARGUMENT; }
    nodes[node]=(struct node){0}; return FS_OK;
}
struct ramfs_stats ramfs_get_stats(void)
{
    struct ramfs_stats s={.ready=ready};
    for (int i=0;i<RAMFS_NODES;++i) {
        if (!nodes[i].used) { continue; }
        if (nodes[i].directory) { ++s.directories; } else { ++s.files; }
        if (nodes[i].has_page) { ++s.pages; }
        s.bytes+=nodes[i].length;
    }
    return s;
}
static enum fs_result reset(void)
{
    ready=false; bool good=true;
    for (int i=0;i<RAMFS_NODES;++i) {
        if (nodes[i].used && nodes[i].has_page) {
            if (!page_free(nodes[i].page)) { good=false; }
            else { nodes[i].has_page=false; }
        }
    }
    if (!good) { return FS_BAD_ARGUMENT; }
    for (int i=0;i<RAMFS_NODES;++i) { nodes[i]=(struct node){0}; }
    return FS_OK;
}
enum fs_result ramfs_init(const char *board)
{
    enum fs_result r=reset(); if (r!=FS_OK) { return r; }
    if (!board) { return FS_BAD_ARGUMENT; }
    make_node(0,0,"",true); ready=true;
    const char *directories[]={"/etc","/home","/home/user","/tmp","/boot","/dev","/proc","/sys","/var","/var/log","/mnt"};
    for (unsigned i=0;i<sizeof(directories)/sizeof(directories[0]);++i) {
        r=ramfs_mkdir(0,directories[i]); if (r!=FS_OK) { goto failed; }
    }
    static const char readme[]=
        "Welcome to OurOS. This root filesystem lives in RAM.\n"
        "Commands: pwd, ls, cd, cat, mkdir, touch, write, rm, rmdir, fsinfo.\n"
        "Try these commands on separate lines:\nmkdir /tmp/demo\nwrite /tmp/demo/note.txt hello\n"
        "RAM files disappear after reset. storageinit tries SD/FAT32 on Pi or virtio on QEMU.\n"
        "USB boot uses firmware; USB disk I/O is pending. Try usbinfo and usbprobe.\n";
    r=ramfs_write(0,"/README.txt",readme,sizeof(readme)-1); if (r!=FS_OK) { goto failed; }
    r=ramfs_write(0,"/etc/hostname","ouros\n",6); if (r!=FS_OK) { goto failed; }
    char board_text[64]; size_t n=0;
    while (board[n] && n<sizeof(board_text)-2) { board_text[n]=board[n]; ++n; }
    if (board[n]) { r=FS_BAD_ARGUMENT; goto failed; }
    board_text[n++]='\n';
    r=ramfs_write(0,"/etc/board",board_text,n); if (r!=FS_OK) { goto failed; }
    return FS_OK;
failed:
    (void)reset(); return r;
}
const char *ramfs_error(enum fs_result result)
{
    static const char *const messages[]={"ok","path not found","not a directory","is a directory","already exists",
        "invalid path","name too long","path too long","node table full","out of physical pages",
        "file too large for this operation","directory not empty","root or current directory is busy","RAM filesystem unavailable","invalid argument",
        "read-only filesystem","storage I/O error","corrupt FAT32 filesystem","unsupported filesystem format"};
    if ((unsigned)result>=sizeof(messages)/sizeof(messages[0])) { return "unknown filesystem error"; }
    return messages[result];
}
