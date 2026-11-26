#include "package.h"
#include "console.h"
#include "crypto.h"
#include "ecdsa.h"
#include "elf_app.h"
#include "memory.h"
#include "mmu.h"
#include "process.h"
#include "sha256.h"
#include "trust_time.h"
#include "vfs.h"

enum { NAMES=17, VERSIONS=16, PATHS=24, FILES=4, DEPS=3, ACTIVE=4, CATALOG=8, MAX_REPOSITORY=65536 };
struct identity { char name[NAMES],version[VERSIONS]; };
struct member { char name[PATHS];uint32_t bytes;unsigned char digest[32]; };
struct release {
    struct identity id,deps[DEPS];
    struct member files[FILES];
    unsigned char file_count,dep_count,slot;
};
struct registry { char magic[8];struct release apps[ACTIVE]; };
struct bundle { struct release release;const unsigned char *payload[FILES]; };
struct catalog { struct bundle bundles[CATALOG];unsigned count; };
struct cursor { const unsigned char *p;size_t remaining; };
struct selection { const struct release *release;const struct bundle *bundle;bool ready; };
struct plan { struct selection selected[ACTIVE];unsigned count; };
static const char root[]="/data/.pkg", index_path[]="/data/.pkg/index";
static unsigned char anchor[32];
static bool have_anchor;

static size_t length(const char *s) { size_t n=0;while (s[n]) { ++n; }return n; }
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static char *append(char *to,const char *from) { while (*from) { *to++=*from++; }*to=0;return to; }
static void copy(void *to,const void *from,size_t n)
{
    unsigned char *a=to;const unsigned char *b=from;
    while (n--) { *a++=*b++; }
}
static void digest(const void *p,size_t n,unsigned char out[32])
{
    struct sha256 sum;sha256_init(&sum);sha256_update(&sum,p,n);sha256_final(&sum,out);
}
static bool label(const char *value,size_t cap,bool version)
{
    if (!value[0]) { return false; }
    for (size_t i=0;i<cap;++i) {
        char c=value[i];
        if (!c) { return true; }
        bool alnum=(c>='a' && c<='z') || (c>='0' && c<='9');
        if (version) { alnum|=c>='A' && c<='Z'; }
        if (!alnum && !(i && (c=='-' || c=='_' || (version && (c=='.' || c=='+'))))) { return false; }
    }
    return false;
}
static bool filename(const char *value)
{
    if (!value[0]) { return false; }
    for (unsigned i=0;i<PATHS;++i) {
        char c=value[i];
        if (!c) { return true; }
        bool alnum=(c>='a' && c<='z') || (c>='0' && c<='9');
        if (!alnum && !(i && (c=='-' || c=='_' || c=='.'))) { return false; }
    }
    return false;
}
static bool valid_id(const struct identity *id)
{ return label(id->name,sizeof(id->name),false) && label(id->version,sizeof(id->version),true); }
static bool same_id(const struct identity *a,const struct identity *b)
{ return same(a->name,b->name) && same(a->version,b->version); }
static bool valid_release(const struct release *r)
{
    if (!valid_id(&r->id) || !r->file_count || r->file_count>FILES || r->dep_count>DEPS) { return false; }
    for (unsigned i=0;i<r->file_count;++i) {
        if (!filename(r->files[i].name) || r->files[i].bytes>VFS_FILE_MAX) { return false; }
        for (unsigned j=0;j<i;++j) { if (same(r->files[i].name,r->files[j].name)) { return false; } }
    }
    for (unsigned i=0;i<r->dep_count;++i) {
        if (!valid_id(&r->deps[i]) || same(r->id.name,r->deps[i].name)) { return false; }
        for (unsigned j=0;j<i;++j) { if (same(r->deps[i].name,r->deps[j].name)) { return false; } }
    }
    return true;
}
static const unsigned char *take(struct cursor *c,size_t n)
{
    if (n>c->remaining) { return NULL; }
    const unsigned char *p=c->p;c->p+=n;c->remaining-=n;return p;
}
static uint32_t little32(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static bool verified_catalog(const unsigned char *raw,size_t bytes,struct catalog *repo)
{
    if (bytes<144 || !crypto_equal(raw,"OUROSRP1",8) || !raw[8] || raw[8]>CATALOG) { return false; }
    for (unsigned i=9;i<16;++i) { if (raw[i]) { return false; } }
    unsigned char hash[32];digest(raw+16,64,hash);
    if (!crypto_equal(anchor,hash,32)) { return false; }
    digest(raw,bytes-64,hash);
    if (!ecdsa_p256_verify(raw+16,hash,raw+bytes-64,raw+bytes-32)) { return false; }
    struct cursor c={raw+80,bytes-144};
    repo->count=raw[8];
    for (unsigned i=0;i<repo->count;++i) {
        struct bundle *b=&repo->bundles[i];struct release *r=&b->release;
        const unsigned char *p=take(&c,sizeof(r->id)+2);
        if (!p) { return false; }
        copy(&r->id,p,sizeof(r->id));r->file_count=p[sizeof(r->id)];r->dep_count=p[sizeof(r->id)+1];
        if (!valid_id(&r->id) || !r->file_count || r->file_count>FILES || r->dep_count>DEPS) { return false; }
        for (unsigned j=0;j<i;++j) { if (same_id(&r->id,&repo->bundles[j].release.id)) { return false; } }
        p=take(&c,r->dep_count*sizeof(r->deps[0]));
        if (!p) { return false; }
        copy(r->deps,p,r->dep_count*sizeof(r->deps[0]));
        for (unsigned j=0;j<r->file_count;++j) {
            struct member *member=&r->files[j];
            p=take(&c,PATHS+4+32);
            if (!p) { return false; }
            copy(member->name,p,PATHS);member->bytes=little32(p+PATHS);copy(member->digest,p+PATHS+4,32);
            if (!filename(member->name) || member->bytes>VFS_FILE_MAX) { return false; }
            b->payload[j]=take(&c,member->bytes);
            if (!b->payload[j]) { return false; }
            digest(b->payload[j],member->bytes,hash);
            if (!crypto_equal(hash,member->digest,32)) { return false; }
            if (same(member->name,"main.elf")) {
                struct app_image_plan image;
                if (app_elf_plan(b->payload[j],member->bytes,&image)!=APP_ELF_OK) { return false; }
            }
        }
        if (!valid_release(r)) { return false; }
    }
    return c.remaining==0;
}
static int find(const struct registry *state,const char *name)
{
    for (unsigned i=0;i<ACTIVE;++i) { if (state->apps[i].slot && same(name,state->apps[i].id.name)) { return (int)i; } }
    return -1;
}
static bool dependencies(const struct registry *state)
{
    for (unsigned i=0;i<ACTIVE;++i) {
        const struct release *r=&state->apps[i];
        if (!r->slot) { continue; }
        for (unsigned j=0;j<r->dep_count;++j) {
            int found=find(state,r->deps[j].name);
            if (found<0 || !same_id(&r->deps[j],&state->apps[found].id)) { return false; }
        }
    }
    return true;
}
static enum fs_result load_registry(struct registry *state)
{
    *state=(struct registry){.magic="OUROSPK1"};
    uint32_t size;enum fs_result result=vfs_file_size(index_path,&size);
    if (result==FS_NOT_FOUND) { return FS_OK; }
    if (result!=FS_OK) { return result; }
    if (size!=sizeof(*state)) { return FS_CORRUPT; }
    size_t bytes;
    result=vfs_read(index_path,0,state,sizeof(*state),&bytes);
    if (result!=FS_OK) { return result; }
    if (bytes!=sizeof(*state) || !crypto_equal(state->magic,"OUROSPK1",8)) { return FS_CORRUPT; }
    for (unsigned i=0;i<ACTIVE;++i) {
        const struct release *r=&state->apps[i];
        if (r->slot && (r->slot>2 || !valid_release(r))) { return FS_CORRUPT; }
    }
    for (unsigned i=0;i<ACTIVE;++i) {
        const struct release *r=&state->apps[i];
        if (r->slot && find(state,r->id.name)!=(int)i) { return FS_CORRUPT; }
    }
    return dependencies(state) ? FS_OK : FS_CORRUPT;
}
static void application_dir(char out[RAMFS_PATH_MAX],const char *name,unsigned slot)
{
    char *end=append(out,root);*end++='/';end=append(end,name);
    *end++='-';*end++=(char)('0'+slot);*end=0;
}
static void member_path(char out[RAMFS_PATH_MAX],const struct release *r,const char *name)
{
    application_dir(out,r->id.name,r->slot);
    char *end=out+length(out);*end++='/';append(end,name);
}
enum fs_result package_path(const char *name,char out[RAMFS_PATH_MAX])
{
    if (!label(name,NAMES,false)) { return FS_INVALID_PATH; }
    struct registry state;enum fs_result result=load_registry(&state);
    if (result!=FS_OK) { return result; }
    int index=find(&state,name);
    if (index<0) { return FS_NOT_FOUND; }
    application_dir(out,name,state.apps[index].slot);
    return FS_OK;
}
static enum fs_result verify_member(const char *path,const struct member *member)
{
    uint32_t size;enum fs_result result=vfs_file_size(path,&size);
    if (result!=FS_OK) { return result; }
    if (size!=member->bytes) { return FS_CORRUPT; }
    struct sha256 sum;sha256_init(&sum);
    unsigned char buffer[512],hash[32];
    for (uint32_t offset=0;offset<size;) {
        size_t bytes,want=size-offset;
        if (want>sizeof(buffer)) { want=sizeof(buffer); }
        result=vfs_read(path,offset,buffer,want,&bytes);
        if (result!=FS_OK) { return result; }
        if (bytes!=want) { return FS_IO; }
        sha256_update(&sum,buffer,bytes);offset+=(uint32_t)bytes;
    }
    sha256_final(&sum,hash);
    return crypto_equal(hash,member->digest,32) ? FS_OK : FS_CORRUPT;
}
static enum fs_result verify_release(const struct release *r)
{
    for (unsigned i=0;i<r->file_count;++i) {
        char path[RAMFS_PATH_MAX];member_path(path,r,r->files[i].name);
        enum fs_result result=verify_member(path,&r->files[i]);
        if (result!=FS_OK) { return result; }
    }
    return FS_OK;
}
static enum fs_result select_release(const struct catalog *repo,const struct registry *state,
    const struct identity *id,struct plan *plan)
{
    for (unsigned i=0;i<plan->count;++i) {
        const struct selection *s=&plan->selected[i];
        if (same(s->release->id.name,id->name)) {
            return s->ready && same_id(&s->release->id,id) ? FS_OK : FS_BUSY;
        }
    }
    if (plan->count==ACTIVE) { return FS_NO_NODES; }
    struct selection *s=&plan->selected[plan->count];
    int old=find(state,id->name);
    if (old>=0 && same_id(&state->apps[old].id,id)) { s->release=&state->apps[old]; }
    else {
        for (unsigned i=0;i<repo->count;++i) {
            const struct bundle *b=&repo->bundles[i];
            if (same_id(&b->release.id,id)) { s->bundle=b;s->release=&b->release;break; }
        }
        if (!s->release) { return FS_NOT_FOUND; }
    }
    ++plan->count;
    for (unsigned i=0;i<s->release->dep_count;++i) {
        enum fs_result result=select_release(repo,state,&s->release->deps[i],plan);
        if (result!=FS_OK) { return result; }
    }
    s->ready=true;return FS_OK;
}
struct directory_files { char names[FILES][PATHS];unsigned count;bool invalid; };
static void remember_file(const char *name,bool dir,size_t bytes,void *opaque)
{
    (void)bytes;
    struct directory_files *contents=opaque;
    if (dir || contents->count==FILES || !filename(name)) { contents->invalid=true;return; }
    append(contents->names[contents->count++],name);
}
static enum fs_result clear_slot(const char *name,unsigned slot)
{
    char path[RAMFS_PATH_MAX];application_dir(path,name,slot);
    struct directory_files contents={0};
    enum fs_result result=vfs_list(path,remember_file,&contents);
    if (result==FS_NOT_FOUND) { return FS_OK; }
    if (result!=FS_OK) { return result; }
    if (contents.invalid) { return FS_CORRUPT; }
    char *end=path+length(path);*end='/';
    for (unsigned i=0;i<contents.count;++i) {
        append(end+1,contents.names[i]);result=vfs_remove(path,false);
        if (result!=FS_OK) { return result; }
    }
    *end=0;
    return vfs_remove(path,true);
}
static enum fs_result stage(const struct release *r,const struct bundle *bundle)
{
    enum fs_result result=clear_slot(r->id.name,r->slot);
    if (result!=FS_OK) { return result; }
    char path[RAMFS_PATH_MAX];application_dir(path,r->id.name,r->slot);
    result=vfs_mkdir(path);
    if (result!=FS_OK) { return result; }
    for (unsigned i=0;i<r->file_count;++i) {
        member_path(path,r,r->files[i].name);
        result=vfs_write(path,bundle->payload[i],r->files[i].bytes);
        if (result!=FS_OK) { return result; }
    }
    return verify_release(r);
}
static enum fs_result install_catalog(const struct catalog *repo,const struct identity *requested)
{
    struct registry old;
    enum fs_result result=load_registry(&old);
    if (result!=FS_OK) { return result; }
    struct plan plan={0};
    result=select_release(repo,&old,requested,&plan);
    if (result!=FS_OK) { return result; }
    struct registry next=old;
    bool changed=false;
    for (unsigned i=0;i<plan.count;++i) {
        const struct selection *s=&plan.selected[i];
        if (!s->bundle) {
            result=verify_release(s->release);
            if (result!=FS_OK) { return result; }
            continue;
        }
        int at=find(&old,s->release->id.name);
        if (at<0) {
            for (at=0;at<ACTIVE && next.apps[at].slot;++at) {}
            if (at==ACTIVE) { return FS_NO_NODES; }
        }
        next.apps[at]=*s->release;
        next.apps[at].slot=old.apps[at].slot==1 ? 2 : 1;
        changed=true;
    }
    if (!dependencies(&next)) { return FS_BUSY; }
    if (!changed) { return FS_OK; }
    result=vfs_mkdir(root);
    if (result!=FS_OK && result!=FS_EXISTS) { return result; }
    for (unsigned i=0;i<plan.count;++i) {
        const struct selection *s=&plan.selected[i];
        if (!s->bundle) { continue; }
        result=stage(&next.apps[find(&next,s->release->id.name)],s->bundle);
        if (result!=FS_OK) { return result; }
    }
    /* This single commit publishes the entire set of staged dependencies. */
    return vfs_write(index_path,&next,sizeof(next));
}
static enum fs_result install(const char *source,const struct identity *requested)
{
    if (!have_anchor) { return FS_ACCESS; }
    uint32_t bytes;enum fs_result result=vfs_file_size(source,&bytes);
    if (result!=FS_OK) { return result; }
    if (bytes<144 || bytes>MAX_REPOSITORY) { return FS_FILE_TOO_LARGE; }
    uint64_t physical;unsigned pages=(bytes+4095)/4096;
    if (!page_alloc_run(pages,&physical)) { return FS_NO_MEMORY; }
    if (!mmu_dma_buffer(physical,(size_t)pages*4096)) { result=FS_NO_MEMORY;goto release; }
    unsigned char *raw=(void *)(uintptr_t)physical;
    size_t read;
    result=vfs_read(source,0,raw,bytes,&read);
    if (result==FS_OK) {
        struct catalog repo={0};
        if (read!=bytes) { result=FS_IO; }
        else if (!verified_catalog(raw,bytes,&repo)) { result=FS_CORRUPT; }
        else {
            vfs_package_edit(true);
            result=install_catalog(&repo,requested);
            vfs_package_edit(false);
        }
    }
    crypto_wipe(raw,bytes);
release:
    for (unsigned i=0;i<pages;++i) { if (!page_free(physical+(uint64_t)i*4096)) { result=FS_IO; } }
    return result;
}
static enum fs_result remove_package(const char *name)
{
    struct registry state;enum fs_result result=load_registry(&state);
    if (result!=FS_OK) { return result; }
    int at=find(&state,name);
    if (at>=0) {
        state.apps[at]=(struct release){0};
        if (!dependencies(&state)) { return FS_BUSY; }
        result=vfs_write(index_path,&state,sizeof(state));
        if (result!=FS_OK) { return result; }
    }
    result=clear_slot(name,1);
    return result==FS_OK ? clear_slot(name,2) : result;
}
static enum fs_result start(const char *name)
{
    struct registry state;enum fs_result result=load_registry(&state);
    if (result!=FS_OK) { return result; }
    int at=find(&state,name);
    if (at<0) { return FS_NOT_FOUND; }
    const struct release *r=&state.apps[at];
    bool has_program=false;
    for (unsigned i=0;i<r->file_count;++i) { has_program|=same(r->files[i].name,"main.elf"); }
    if (!has_program) { return FS_UNSUPPORTED; }
    for (unsigned i=0;i<ACTIVE;++i) {
        if (!state.apps[i].slot) { continue; }
        result=verify_release(&state.apps[i]);
        if (result!=FS_OK) { return result; }
    }
    struct app_args args={.count=1};
    member_path(args.bytes,r,"main.elf");args.used=(unsigned)length(args.bytes)+1;
    return process_exec(&args)==0 ? FS_OK : FS_IO;
}
bool package_command(const char *verb,const char *arguments)
{
    bool trust=same(verb,"pkgtrust"),apply=same(verb,"pkginstall"),list=same(verb,"pkglist");
    bool run=same(verb,"pkgstart"),remove=same(verb,"pkgremove"),path=same(verb,"pkgpath");
    if (!trust && !apply && !list && !run && !remove && !path) { return false; }
    while (*arguments==' ') { ++arguments; }
    struct app_args args={0};
    if (*arguments && !app_args_parse(arguments,&args)) { goto usage; }
    if (trust) {
        if (args.count!=1) { goto usage; }
        if (same(args.bytes,"clear")) { crypto_wipe(anchor,sizeof(anchor));have_anchor=false; }
        else {
            unsigned char key[32];
            if (!trust_parse_digest(args.bytes,key)) { goto usage; }
            copy(anchor,key,sizeof(anchor));have_anchor=true;
        }
        console_write(have_anchor ? "Package signing key pinned.\n" : "Package key pin cleared.\n");
        return true;
    }
    struct vfs_volume_info volume;
    enum fs_result result=vfs_volume_info("/data",&volume);
    if (result!=FS_OK || volume.version!=5) {
        console_write("Package installer requires mounted /data v5.\n");return true;
    }
    const char *name=args.bytes;
    if (apply) {
        if (args.count!=3) { goto usage; }
        name=args.bytes+args.offset[1];const char *version=args.bytes+args.offset[2];
        if (!label(name,NAMES,false) || !label(version,VERSIONS,true) || !name[0] || args.bytes[0]!='/') { goto usage; }
        struct identity id={0};append(id.name,name);append(id.version,version);
        char source[RAMFS_PATH_MAX];bool dir;
        result=vfs_resolve("/",args.bytes,source,&dir);
        if (result==FS_OK) { result=dir ? FS_IS_DIR : install(source,&id); }
        if (result==FS_OK) { console_write("Package ready ");console_write(name);console_write(" ");console_write(version);console_write("\n"); }
    } else if (list) {
        if (args.count) { goto usage; }
        struct registry state;result=load_registry(&state);
        if (result==FS_OK) {
            bool found=false;
            for (unsigned i=0;i<ACTIVE;++i) {
                const struct release *r=&state.apps[i];
                if (!r->slot) { continue; }
                found=true;console_write(r->id.name);console_write(" ");console_write(r->id.version);console_write("\n");
            }
            if (!found) { console_write("No installed packages.\n"); }
        }
    } else {
        if (args.count!=1 || !label(name,NAMES,false)) { goto usage; }
        if (run) { result=start(name); }
        else if (path) {
            char out[RAMFS_PATH_MAX];result=package_path(name,out);
            if (result==FS_OK) { console_write(out);console_write("\n"); }
        } else {
            vfs_package_edit(true);result=remove_package(name);vfs_package_edit(false);
            if (result==FS_OK) { console_write("Package removed ");console_write(name);console_write("\n"); }
        }
    }
    if (result!=FS_OK) {
        console_write("Package operation failed: ");console_write(ramfs_error(result));console_write("\n");
    }
    return true;
usage:
    console_write("pkgtrust <SHA-256|clear> | pkginstall <repository> <name> <version>\n");
    console_write("pkglist | pkgpath <name> | pkgstart <name> | pkgremove <name>\n");
    return true;
}
