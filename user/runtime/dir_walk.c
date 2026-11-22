#include "dir_walk.h"

enum { DIRECTORY_LIMIT=1024,ENTRY_LIMIT=4096,FRAME_LIMIT=DIR_WALK_PATH_BYTES/2 };
struct directory_frame { uint16_t length,next; };

const char *dir_walk_error(enum dir_walk_result result)
{
    switch (result) {
    case DIR_WALK_MISSING:return "path unavailable\n";
    case DIR_WALK_READ:return "directory read failed\n";
    case DIR_WALK_LIMIT:return "path or directory limit reached\n";
    default:return "output failed\n";
    }
}

static const char *basename(const char *path,size_t bytes)
{
    size_t start=bytes;
    while (start && path[start-1]!='/') { --start; }
    return start==bytes ? path : path+start;
}

enum dir_walk_result dir_walk(char path[DIR_WALK_PATH_BYTES],dir_walk_visit visit,void *context)
{
    struct uos_entry entry;
    if (uos_stat(path,&entry)) { return DIR_WALK_MISSING; }
    if (entry.type!=UOS_FILE && entry.type!=UOS_DIRECTORY) { return DIR_WALK_READ; }
    size_t bytes=uos_length(path);
    while (bytes>1 && path[bytes-1]=='/') { path[--bytes]=0; }
    if (!visit(path,basename(path,bytes),&entry,context)) { return DIR_WALK_STOP; }
    if (entry.type==UOS_FILE) { return DIR_WALK_OK; }

    struct directory_frame frames[FRAME_LIMIT]={{(uint16_t)bytes,0}};
    unsigned depth=0,seen=1;
    for (;;) {
        struct directory_frame *frame=&frames[depth];
        bytes=frame->length;
        path[bytes]=0;
        if (frame->next==DIRECTORY_LIMIT) { return DIR_WALK_LIMIT; }
        int64_t result=uos_readdir(path,frame->next++,&entry);
        if (result<0) { return DIR_WALK_READ; }
        if (!result) {
            if (!depth) { return DIR_WALK_OK; }
            --depth;
            continue;
        }
        if (seen++==ENTRY_LIMIT) { return DIR_WALK_LIMIT; }
        if (entry.type!=UOS_FILE && entry.type!=UOS_DIRECTORY) { return DIR_WALK_READ; }
        size_t n=0;
        while (n<sizeof(entry.name) && entry.name[n] && entry.name[n]!='/') { ++n; }
        bool dot=entry.name[0]=='.' && (n==1 || (n==2 && entry.name[1]=='.'));
        if (!n || n==sizeof(entry.name) || entry.name[n] || dot) { return DIR_WALK_READ; }
        bool separate=path[bytes-1]!='/';
        if (n+separate>=DIR_WALK_PATH_BYTES-bytes) { return DIR_WALK_LIMIT; }
        if (separate) { path[bytes++]='/'; }
        for (size_t i=0;i<=n;++i) { path[bytes+i]=entry.name[i]; }
        if (!visit(path,entry.name,&entry,context)) { return DIR_WALK_STOP; }
        if (entry.type==UOS_DIRECTORY) {
            if (depth+1==FRAME_LIMIT) { return DIR_WALK_LIMIT; }
            frames[++depth]=(struct directory_frame){(uint16_t)(bytes+n),0};
        }
    }
}
