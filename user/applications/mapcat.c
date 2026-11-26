#include "user.h"

enum { WINDOW_BYTES=8192 };

static bool size_arg(unsigned index,uint64_t *value)
{
    char text[21];
    if (uos_arg(index,text,sizeof(text))<=0) { return false; }
    uint64_t n=0;
    for (const char *p=text;*p;++p) {
        unsigned digit=(unsigned)(*p-'0');
        if (digit>9 || n>(UINT64_MAX-digit)/10) { return false; }
        n=n*10+digit;
    }
    *value=n;
    return true;
}

static bool write_window(uint64_t handle,uint64_t offset,size_t length)
{
    uint64_t token;
    int64_t address=uos_vm_file(handle,offset,length,UOS_READ,&token);
    if (address<0) { uos_puts("mapcat: cannot map file window\n");return false; }
    const unsigned char *view=(const void *)(uintptr_t)address;
    bool good=true;
    while (length) {
        size_t count=length>512 ? 512 : length;
        if (uos_write(view,count)!=(int64_t)count) { good=false;break; }
        view+=count;length-=count;
    }
    return uos_vm_unmap(token)==0 && good;
}

int app_main(void)
{
    char path[256];
    int64_t argc=uos_argc();
    uint64_t offset=0,length=UINT64_MAX;
    if (argc<2 || argc>4 || uos_arg(1,path,sizeof(path))<=0 ||
        (argc>=3 && !size_arg(2,&offset)) || (argc==4 && !size_arg(3,&length))) {
        uos_puts("Usage: mapcat PATH [OFFSET [LENGTH]]\n");return 2;
    }
    int64_t handle=uos_open(path,UOS_READ);
    if (handle<0) { uos_puts("mapcat: cannot open file\n");return 1; }
    struct uos_file_info info;
    if (uos_fstat((uint64_t)handle,&info) || offset>info.bytes) {
        (void)uos_close((uint64_t)handle);
        uos_puts("mapcat: offset outside file\n");return 1;
    }
    uint64_t available=info.bytes-offset;
    if (length>available) { length=available; }
    bool good=true;
    while (length) {
        size_t window=length>WINDOW_BYTES ? WINDOW_BYTES : (size_t)length;
        if (!write_window((uint64_t)handle,offset,window)) { good=false;break; }
        offset+=window;length-=window;
    }
    return uos_close((uint64_t)handle)!=0 || !good;
}
