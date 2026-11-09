#ifndef OUR_OS_HOST_ASSERT_H
#define OUR_OS_HOST_ASSERT_H
#include <stdio.h>
#include <stdlib.h>
/* Native test assertions should report to the terminal and return failure.
 * CRT abort() can leave a Windows Error Reporting dialog waiting for a click.
 * Keep checks enabled even if a host build defines NDEBUG. Kernel unchanged. */
static inline _Noreturn void host_assert_failed(const char *expression,const char *file,int line)
{
    fprintf(stderr,"FAIL: %s:%d: %s\n",file,line,expression);
    fflush(stderr);
    exit(EXIT_FAILURE);
}
#undef assert
#define assert(expression) do { if (!(expression)) { host_assert_failed(#expression,__FILE__,__LINE__); } } while (0)
#endif
