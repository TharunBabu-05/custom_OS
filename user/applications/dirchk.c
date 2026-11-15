#include "user.h"
static int same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
int app_main(void)
{
    const char *root="/data/.dirchk",*sub="/data/.dirchk/sub",*other="/data/.dirchk/other";
    struct uos_entry e;unsigned char data[512],hash[32],actual[32];char cwd[256];
    if (uos_stat(root,&e)!=UOS_ERROR) { return 1; }
    uos_puts("DIRCHK: v3 nested paths, cwd, binary move and directory cleanup; wait for PASS.\n");
    if (uos_mkdir(root) || uos_mkdir(sub) || uos_mkdir(other)) { return 2; }
    if (uos_stat(sub,&e) || e.type!=UOS_DIRECTORY || e.bytes || uos_readdir(root,0,&e)!=1 || e.type!=UOS_DIRECTORY ||
        uos_readdir(root,1,&e)!=1 || e.type!=UOS_DIRECTORY || uos_readdir(root,2,&e)!=0) { return 3; }
    for (unsigned i=0;i<512;++i) { data[i]=(unsigned char)i; }
    if (uos_replace("/data/.dirchk/sub/note",data,512)!=512 || uos_replace("/data/.dirchk/other/note","other",5)!=5 ||
        uos_filehash("/data/.dirchk/sub/note",hash) || uos_remove(root,1)!=UOS_ERROR) { return 4; }
    int64_t h=uos_open("/data/.dirchk/sub/note",UOS_READ);if (h<0 || uos_chdir(sub)) { return 5; }
    if (uos_getcwd(cwd,sizeof(cwd))<0 || !same(cwd,sub) || uos_rename("note","../other/moved")!=UOS_EBUSY ||
        uos_close((uint64_t)h) || uos_rename("./note","../other/moved") || uos_stat("note",&e)!=UOS_ERROR ||
        uos_filehash("../other/moved",actual)) { return 6; }
    for (unsigned i=0;i<32;++i) { if (hash[i]!=actual[i]) { return 7; } }
    if (uos_remove(".",1)!=UOS_ERROR || uos_replace("note","keep",4)!=4 || uos_rename("note","../other/moved")!=UOS_EEXIST ||
        uos_remove("note",0) || uos_chdir("../other") || uos_remove("../sub",1)) { return 8; }
    if (uos_stat("note",&e) || e.type!=UOS_FILE || e.bytes!=5 || uos_replace("moved/child","x",1)!=UOS_ERROR ||
        uos_mkdir("moved/child")!=UOS_ERROR || uos_remove("note",0) || uos_remove("moved",0) || uos_remove(".",1)!=UOS_ERROR) { return 9; }
    if (uos_chdir("/") || uos_remove(other,1) || uos_remove(root,1) || uos_stat(root,&e)!=UOS_ERROR || uos_remove("/data",1)!=UOS_ERROR) { return 10; }
    uos_puts("PASS: persistent nested mkdir/cwd/list, sibling names, binary move/hash, open-file and cwd pins, path/type/nonempty refusal and cleanup.\n");return 0;
}
