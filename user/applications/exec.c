/* Small chain-loader for exercising exec in redirected jobs and pipelines. */
#include "user.h"
int app_main(void)
{
    struct app_args args={0};int64_t argc=uos_argc();
    if (argc<2 || argc>APP_ARG_LIMIT) { uos_puts("Usage: EXEC.ELF <ELF path> [args]\n");return 2; }
    for (unsigned i=1;i<(unsigned)argc;++i) {
        args.offset[args.count++]=args.used;
        int64_t n=uos_arg(i,args.bytes+args.used,sizeof(args.bytes)-args.used);
        if (n<0) { return 2; }args.used+=(unsigned)n+1;
    }
    (void)uos_exec(&args);uos_puts("exec: image replacement failed\n");return 126;
}
