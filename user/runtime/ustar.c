#include "ustar.h"

enum {
    NAME_BYTES=100, MODE=100, UID=108, GID=116, SIZE=124, MTIME=136,
    CHECKSUM=148, TYPE=156, MAGIC=257, VERSION=263, DEVMAJOR=329, DEVMINOR=337,
    PREFIX=345, PREFIX_BYTES=155, COMPONENT_BYTES=31
};

static const unsigned char magic[]={'u','s','t','a','r',0,'0','0'};
_Static_assert(MAGIC+sizeof(magic)==VERSION+2,"ustar signature layout");

static size_t bounded_length(const unsigned char *bytes,size_t limit)
{
    size_t n=0;
    while (n<limit && bytes[n]) { ++n; }
    return n;
}

static bool zeroes(const unsigned char *bytes,size_t count)
{
    for (size_t i=0;i<count;++i) { if (bytes[i]) { return false; } }
    return true;
}

static bool path_valid(const char *path,size_t bytes)
{
    if (!bytes || bytes>=USTAR_PATH) { return false; }
    size_t start=0;
    for (size_t i=0;i<=bytes;++i) {
        char c=path[i];
        if (i==bytes || c=='/') {
            size_t n=i-start;
            if (!n || n>COMPONENT_BYTES ||
                (path[start]=='.' && (n==1 || (n==2 && path[start+1]=='.')))) { return false; }
            start=i+1;
        } else if (!((c>='A' && c<='Z') || (c>='a' && c<='z') ||
                     (c>='0' && c<='9') || c=='.' || c=='_' || c=='-')) { return false; }
    }
    return true;
}

static bool octal_read(const unsigned char *field,size_t bytes,uint32_t *out)
{
    size_t i=0;
    while (i<bytes && field[i]==' ') { ++i; }
    size_t start=i;
    uint32_t value=0;
    while (i<bytes && field[i]>='0' && field[i]<='7') {
        unsigned digit=field[i++]-'0';
        if (value>(UINT32_MAX-digit)/8) { return false; }
        value=value*8+digit;
    }
    if (i==start) { return false; }
    for (;i<bytes;++i) { if (field[i] && field[i]!=' ') { return false; } }
    *out=value;
    return true;
}

static void octal_write(unsigned char *field,size_t bytes,uint32_t value)
{
    field[--bytes]=0;
    while (bytes) {
        field[--bytes]=(unsigned char)('0'+value%8);
        value/=8;
    }
}

static uint32_t checksum(const unsigned char *header)
{
    uint32_t sum=0;
    for (unsigned i=0;i<USTAR_BLOCK;++i) {
        sum+=i>=CHECKSUM && i<TYPE ? ' ' : header[i];
    }
    return sum;
}

bool ustar_make_header(unsigned char header[USTAR_BLOCK],const char *path,uint32_t bytes,bool directory)
{
    size_t length=bounded_length((const unsigned char *)path,USTAR_PATH);
    if (!path_valid(path,length) || bytes>USTAR_LIMIT || (directory && bytes)) { return false; }
    size_t prefix=0,name=0;
    if (length>NAME_BYTES) {
        for (size_t i=length-1;i>0;--i) {
            if (path[i]=='/' && i<=PREFIX_BYTES && length-i-1<=NAME_BYTES) {
                prefix=i;
                name=i+1;
                break;
            }
        }
        if (!prefix) { return false; }
    }
    for (unsigned i=0;i<USTAR_BLOCK;++i) { header[i]=0; }
    for (size_t i=0;i<prefix;++i) { header[PREFIX+i]=(unsigned char)path[i]; }
    for (size_t i=name;i<length;++i) { header[i-name]=(unsigned char)path[i]; }
    octal_write(header+MODE,8,directory ? 0755 : 0644);
    octal_write(header+UID,8,0);
    octal_write(header+GID,8,0);
    octal_write(header+SIZE,12,bytes);
    octal_write(header+MTIME,12,0);
    octal_write(header+DEVMAJOR,8,0);
    octal_write(header+DEVMINOR,8,0);
    header[TYPE]=directory ? '5' : '0';
    for (size_t i=0;i<sizeof(magic);++i) { header[MAGIC+i]=magic[i]; }
    octal_write(header+CHECKSUM,7,checksum(header));
    header[CHECKSUM+7]=' ';
    return true;
}

static bool member_path(const unsigned char *header,struct ustar_member *member)
{
    size_t prefix=bounded_length(header+PREFIX,PREFIX_BYTES),name=bounded_length(header,NAME_BYTES);
    if (!name || prefix+name+(prefix!=0)>=USTAR_PATH) { return false; }
    size_t at=0;
    for (;at<prefix;++at) { member->path[at]=(char)header[PREFIX+at]; }
    if (prefix) { member->path[at++]='/'; }
    for (size_t i=0;i<name;++i) { member->path[at++]=(char)header[i]; }
    if (member->directory && member->path[at-1]=='/') { --at; }
    member->path[at]=0;
    /* Archives from tar -C DIR . can have a root marker and ./ prefixes. */
    if (member->directory && at==1 && member->path[0]=='.') { member->path[0]=0;return true; }
    if (at>2 && member->path[0]=='.' && member->path[1]=='/') {
        for (size_t i=2;i<=at;++i) { member->path[i-2]=member->path[i]; }
        at-=2;
    }
    return path_valid(member->path,at);
}

void ustar_member_at(const unsigned char *archive,uint16_t offset,struct ustar_member *member)
{
    const unsigned char *header=archive+offset;
    member->directory=header[TYPE]=='5';
    (void)octal_read(header+SIZE,12,&member->bytes);
    (void)member_path(header,member);
}

static bool conflicting_path(const struct ustar_member *a,const struct ustar_member *b)
{
    size_t i=0;
    while (a->path[i] && a->path[i]==b->path[i]) { ++i; }
    if (!a->path[i] && !b->path[i]) { return true; }
    return (!a->path[i] && b->path[i]=='/' && !a->directory) ||
           (!b->path[i] && a->path[i]=='/' && !b->directory);
}

bool ustar_scan(const unsigned char *archive,size_t bytes,struct ustar_index *index)
{
    index->count=0;
    if (bytes<2*USTAR_BLOCK || bytes>USTAR_LIMIT || bytes%USTAR_BLOCK) { return false; }
    struct ustar_index found={0};
    for (size_t at=0;at+2*USTAR_BLOCK<=bytes;) {
        const unsigned char *header=archive+at;
        if (zeroes(header,USTAR_BLOCK)) {
            if (!zeroes(header,bytes-at)) { return false; }
            *index=found;
            return true;
        }
        for (size_t i=0;i<sizeof(magic);++i) { if (header[MAGIC+i]!=magic[i]) { return false; } }
        uint32_t sum;
        struct ustar_member member={.directory=header[TYPE]=='5'};
        if ((!member.directory && header[TYPE] && header[TYPE]!='0') ||
            !octal_read(header+CHECKSUM,8,&sum) || sum!=checksum(header) ||
            !octal_read(header+SIZE,12,&member.bytes) || member.bytes>USTAR_LIMIT ||
            (member.directory && member.bytes) || !member_path(header,&member)) { return false; }
        size_t padded=(member.bytes+USTAR_BLOCK-1)&~(size_t)(USTAR_BLOCK-1);
        if (padded>bytes-at-USTAR_BLOCK) { return false; }
        if (!member.path[0]) {
            if (at) { return false; }
        } else {
            if (found.count==USTAR_MEMBERS) { return false; }
            for (unsigned i=0;i<found.count;++i) {
                struct ustar_member previous;
                ustar_member_at(archive,found.offsets[i],&previous);
                if (conflicting_path(&member,&previous)) { return false; }
            }
            found.offsets[found.count++]=(uint16_t)at;
        }
        at+=USTAR_BLOCK+padded;
    }
    return false;
}
