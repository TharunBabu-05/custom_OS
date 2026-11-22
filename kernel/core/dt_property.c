#include "dt_property.h"
static uint32_t be32(const unsigned char *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a; ++b; } return *a==*b; }
static bool region(uint32_t start,uint32_t length,uint32_t total)
{ return start>=40 && start<=total && length<=total-start; }
static enum dt_lookup lookup(const void *blob,size_t available,const char *wanted,const char *key,
    unsigned index,const unsigned char **value,uint32_t *length)
{
    if (!blob || available<40 || !wanted || !value || !length) { return DT_INVALID; }
    const unsigned char *d=blob;
    uint32_t total=be32(d+4),so=be32(d+8),no=be32(d+12),ns=be32(d+32),ss=be32(d+36);
    if (be32(d)!=0xd00dfeed || total>available || be32(d+20)<17 || be32(d+24)>17 ||
        (so&3) || !region(so,ss,total) || !region(no,ns,total) || (so<no+ns && no<so+ss)) { return DT_INVALID; }
    const unsigned char *p=d+so,*end=p+ss,*found=NULL; uint32_t found_length=0;
    char path[512]=""; unsigned saved[32]={0},depth=0,used=0;
    bool children[32]={0},root=false,closed=false;
    while ((size_t)(end-p)>=4) {
        uint32_t token=be32(p); p+=4;
        if (token==1) {
            if (depth>=32 || closed || (!depth && root)) { return DT_INVALID; }
            const unsigned char *name=p; while (p<end && *p) { if (*p=='/') { return DT_INVALID; } ++p; }
            if (p==end) { return DT_INVALID; } size_t n=(size_t)(p-name); ++p;
            size_t pad=(4-((size_t)(p-d)&3))&3; if ((size_t)(end-p)<pad) { return DT_INVALID; } p+=pad;
            saved[depth]=used; children[depth]=false;
            if (!depth) { if (n) { return DT_INVALID; } root=true; path[0]='/'; path[1]='\0'; used=1; }
            else {
                if (!key && !found && same(path,wanted) && index--==0) { found=name;found_length=(uint32_t)n; }
                children[depth-1]=true;
                if (used+(used>1 ? 1 : 0)+n>=sizeof(path)) { return DT_INVALID; }
                if (used>1) { path[used++]='/'; }
                for (size_t i=0;i<n;++i) { path[used++]=(char)name[i]; } path[used]='\0';
            }
            ++depth;
        } else if (token==2) {
            if (!depth) { return DT_INVALID; } used=saved[--depth]; path[used]='\0'; if (!depth) { closed=true; }
        } else if (token==3) {
            if (!depth || children[depth-1] || (size_t)(end-p)<8) { return DT_INVALID; }
            uint32_t n=be32(p),offset=be32(p+4); p+=8;
            if (n>(size_t)(end-p) || offset>=ns) { return DT_INVALID; }
            uint32_t at=offset; while (at<ns && d[no+at]) { ++at; } if (at==ns) { return DT_INVALID; }
            if (key && same(path,wanted) && same((const char *)d+no+offset,key)) {
                if (found) { return DT_INVALID; } found=p; found_length=n;
            }
            p+=n; size_t pad=(4-(n&3))&3; if ((size_t)(end-p)<pad) { return DT_INVALID; } p+=pad;
        } else if (token==9) {
            if (depth || !closed || !root) { return DT_INVALID; }
            if (!found) { return DT_MISSING; }
            *value=found; *length=found_length; return DT_FOUND;
        } else if (token!=4) { return DT_INVALID; }
    }
    return DT_INVALID;
}

enum dt_lookup dt_optional_property(const void *blob,size_t available,const char *path,const char *property,
                 const unsigned char **value,uint32_t *length)
{
    return property ? lookup(blob,available,path,property,0,value,length) : DT_INVALID;
}
bool dt_property(const void *blob,size_t available,const char *path,const char *property,
                 const unsigned char **value,uint32_t *length)
{
    return dt_optional_property(blob,available,path,property,value,length)==DT_FOUND;
}
enum dt_lookup dt_child(const void *blob,size_t available,const char *parent,unsigned index,const char **name)
{
    if (!name) { return DT_INVALID; }
    const unsigned char *value;
    uint32_t length;
    enum dt_lookup result=lookup(blob,available,parent,NULL,index,&value,&length);
    if (result==DT_FOUND) { *name=(const char *)value; }
    return result;
}
