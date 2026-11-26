#include "user.h"

static bool asset(const char *package,const char *file,char out[64])
{
    char path[96];
    int64_t n=uos_package_path(package,path,sizeof(path));
    if (n<=0 || (uint64_t)n+1+uos_length(file)>=sizeof(path)) { return false; }
    path[n++]='/';
    do { path[n++]=*file; } while (*file++);
    int64_t handle=uos_open(path,UOS_READ);
    if (handle<0) { return false; }
    n=uos_read((uint64_t)handle,out,63);
    int64_t closed=uos_close((uint64_t)handle);
    if (closed || n<=0) { return false; }
    out[n]=0;
    return uos_open(path,UOS_WRITE)<0;
}

int app_main(void)
{
    char local[64],dependency[64];
    if (!asset("pkgdemo","message.txt",local) || !asset("pkgbase","version.txt",dependency)) {
        uos_puts("FAIL: package assets or protected writes.\n");return 1;
    }
    uos_puts("Package asset ");uos_puts(local);uos_puts("\n");
    uos_puts("Dependency asset ");uos_puts(dependency);uos_puts("\n");
    uos_puts("PASS: package executable, bundled asset, dependency asset and protected writes.\n");
    return 0;
}
