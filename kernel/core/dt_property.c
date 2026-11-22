#include "dt_property.h"

enum { FDT_BEGIN=1, FDT_CLOSE=2, FDT_PROPERTY=3, FDT_NOP=4, FDT_END=9 };

static bool same(const char *a,const char *b)
{ while (*a && *a==*b) { ++a; ++b; } return *a==*b; }

static bool region(uint32_t start,uint32_t length,uint32_t total)
{ return start>=40 && start<=total && length<=total-start; }

bool dt_walk(const void *blob,size_t available,dt_visitor visit,void *context)
{
    if (!blob || available<40) { return false; }
    const unsigned char *d=blob;
    uint32_t total=dt_cell(d+4),so=dt_cell(d+8),no=dt_cell(d+12),ns=dt_cell(d+32),ss=dt_cell(d+36);
    if (dt_cell(d)!=0xd00dfeed || total>available || dt_cell(d+20)<17 || dt_cell(d+24)>17 ||
        (so&3) || !region(so,ss,total) || !region(no,ns,total) || (so<no+ns && no<so+ss)) { return false; }

    const unsigned char *p=d+so,*end=p+ss;
    struct dt_node node={.strings=d+no};
    char path[DT_PATH_CAPACITY]="/";
    unsigned saved[32],depth=0,used=0;
    bool pending=false,closed=false;
    while ((size_t)(end-p)>=4) {
        const unsigned char *token_start=p;
        uint32_t token=dt_cell(p);
        p+=4;
        if (token==FDT_BEGIN || token==FDT_CLOSE) {
            if (pending && visit) {
                node.end=token_start;
                visit(path,&node,context);
            }
            pending=false;
        }
        if (token==FDT_BEGIN) {
            if (depth>=32 || closed) { return false; }
            const unsigned char *name=p;
            while (p<end && *p) {
                if (*p<33 || *p>126 || *p=='/') { return false; }
                ++p;
            }
            if (p==end) { return false; }
            size_t n=(size_t)(p-name),field=(n+4)&~(size_t)3;
            if (field>(size_t)(end-name)) { return false; }
            p=name+field;

            saved[depth]=used;
            if (!depth) {
                if (n) { return false; }
                used=1;
                path[0]='/';
            } else {
                if (!n || used+(used>1)+n>=sizeof(path)) { return false; }
                if (used>1) { path[used++]='/'; }
                for (size_t i=0;i<n;++i) { path[used++]=(char)name[i]; }
            }
            path[used]='\0';
            node.name=(const char *)name;
            node.properties=p;
            pending=true;
            ++depth;
        } else if (token==FDT_CLOSE) {
            if (!depth) { return false; }
            used=saved[--depth];
            path[used]='\0';
            closed=!depth;
        } else if (token==FDT_PROPERTY) {
            if (!pending || (size_t)(end-p)<8) { return false; }
            uint32_t n=dt_cell(p),offset=dt_cell(p+4);
            p+=8;
            if (n>(size_t)(end-p) || offset>=ns) { return false; }
            uint32_t at=offset;
            while (at<ns && d[no+at]) { ++at; }
            if (at==ns) { return false; }
            p+=n;
            size_t pad=(4-(n&3))&3;
            if ((size_t)(end-p)<pad) { return false; }
            p+=pad;
        } else if (token==FDT_END) {
            return closed;
        } else if (token!=FDT_NOP) { return false; }
    }
    return false;
}

enum dt_lookup dt_node_property(const struct dt_node *node,const char *key,
                               const unsigned char **value,uint32_t *length)
{
    const unsigned char *found=NULL;
    uint32_t bytes=0;
    for (const unsigned char *p=node->properties; p<node->end;) {
        uint32_t token=dt_cell(p);
        p+=4;
        if (token==FDT_NOP) { continue; }
        uint32_t n=dt_cell(p),offset=dt_cell(p+4);
        p+=8;
        if (same((const char *)node->strings+offset,key)) {
            if (found) { return DT_INVALID; }
            found=p;
            bytes=n;
        }
        p+=n+((4-(n&3))&3);
    }
    if (!found) { return DT_MISSING; }
    *value=found;
    *length=bytes;
    return DT_FOUND;
}

struct node_search {
    const char *path;
    char *found_path;
    struct dt_node node;
    uint32_t id;
    unsigned matches;
};

static void find_node(const char *path,const struct dt_node *node,void *context)
{
    struct node_search *search=context;
    if (search->path) {
        if (!same(path,search->path)) { return; }
    } else {
        const unsigned char *p;
        uint32_t bytes;
        if (dt_node_property(node,"phandle",&p,&bytes)!=DT_FOUND || bytes!=4 ||
            dt_cell(p)!=search->id) { return; }
    }
    if (search->matches++) { return; }
    search->node=*node;
    if (search->found_path) {
        char *out=search->found_path;
        while ((*out++=*path++)) {}
    }
}

static enum dt_lookup search_node(const void *blob,size_t bytes,struct node_search *search,struct dt_node *out)
{
    if (!dt_walk(blob,bytes,find_node,search) || search->matches>1) { return DT_INVALID; }
    if (!search->matches) { return DT_MISSING; }
    *out=search->node;
    return DT_FOUND;
}

enum dt_lookup dt_find_node(const void *blob,size_t bytes,const char *path,struct dt_node *out)
{
    if (!path || !out) { return DT_INVALID; }
    struct node_search search={.path=path};
    return search_node(blob,bytes,&search,out);
}

enum dt_lookup dt_find_phandle(const void *blob,size_t bytes,uint32_t id,struct dt_node *out,
                              char path[DT_PATH_CAPACITY])
{
    if (!id || id==UINT32_MAX || !out) { return DT_INVALID; }
    struct node_search search={.id=id,.found_path=path};
    return search_node(blob,bytes,&search,out);
}

bool dt_direct_child(const char *path,const char *parent)
{
    bool root=parent[0]=='/' && !parent[1];
    while (*parent && *path==*parent) { ++path; ++parent; }
    if (*parent || (!root && *path++!='/') || !*path) { return false; }
    while (*path && *path!='/') { ++path; }
    return !*path;
}

struct child_search { const char *parent,*name;unsigned index; };

static void find_child(const char *path,const struct dt_node *node,void *context)
{
    struct child_search *search=context;
    if (!search->name && dt_direct_child(path,search->parent) && search->index--==0) {
        search->name=node->name;
    }
}

enum dt_lookup dt_optional_property(const void *blob,size_t bytes,const char *path,const char *key,
                                   const unsigned char **value,uint32_t *length)
{
    if (!key || !value || !length) { return DT_INVALID; }
    struct dt_node node;
    enum dt_lookup result=dt_find_node(blob,bytes,path,&node);
    return result==DT_FOUND ? dt_node_property(&node,key,value,length) : result;
}

bool dt_property(const void *blob,size_t bytes,const char *path,const char *key,
                 const unsigned char **value,uint32_t *length)
{ return dt_optional_property(blob,bytes,path,key,value,length)==DT_FOUND; }

enum dt_lookup dt_child(const void *blob,size_t bytes,const char *parent,unsigned index,const char **name)
{
    if (!parent || !name) { return DT_INVALID; }
    struct child_search search={.parent=parent,.index=index};
    if (!dt_walk(blob,bytes,find_child,&search)) { return DT_INVALID; }
    if (!search.name) { return DT_MISSING; }
    *name=search.name;
    return DT_FOUND;
}
