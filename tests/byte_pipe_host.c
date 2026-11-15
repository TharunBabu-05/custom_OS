#include "host_assert.h"
#include "byte_pipe.h"
#include <string.h>
#include <stdio.h>
int main(void)
{
    struct byte_pipe p;unsigned char source[1024],out[1024];size_t n;
    for (unsigned i=0;i<1024;++i) { source[i]=(unsigned char)i; }
    byte_pipe_init(&p);
    assert(byte_pipe_read(&p,out,8,&n)==BYTE_PIPE_AGAIN && !n);
    assert(byte_pipe_write(&p,source,1024)==BYTE_PIPE_OK);
    struct byte_pipe before=p;
    assert(byte_pipe_write(&p,source,1)==BYTE_PIPE_AGAIN && p.head==before.head && p.count==before.count &&
        p.reader==before.reader && p.writer==before.writer && !memcmp(p.bytes,before.bytes,sizeof(p.bytes)));
    assert(byte_pipe_read(&p,out,700,&n)==BYTE_PIPE_OK && n==700 && !memcmp(source,out,n));
    assert(byte_pipe_write(&p,source,700)==BYTE_PIPE_OK);
    assert(byte_pipe_read(&p,out,1024,&n)==BYTE_PIPE_OK && n==1024 && !memcmp(source+700,out,324) && !memcmp(source,out+324,700));
    for (unsigned iteration=0;iteration<10000;++iteration) {
        size_t bytes=iteration%512+1;
        assert(byte_pipe_write(&p,source,bytes)==BYTE_PIPE_OK);
        assert(byte_pipe_read(&p,out,bytes,&n)==BYTE_PIPE_OK && n==bytes && !memcmp(source,out,bytes));
    }
    assert(byte_pipe_write(&p,source,512)==BYTE_PIPE_OK);byte_pipe_close_writer(&p);
    assert(byte_pipe_read(&p,out,1024,&n)==BYTE_PIPE_OK && n==512 && !memcmp(source,out,n));
    assert(byte_pipe_read(&p,out,1,&n)==BYTE_PIPE_OK && !n);
    byte_pipe_init(&p);assert(byte_pipe_write(&p,source,512)==BYTE_PIPE_OK);byte_pipe_close_reader(&p);
    assert(byte_pipe_write(&p,source,1)==BYTE_PIPE_BROKEN && !p.count);
    for (unsigned i=0;i<1024;++i) { assert(!p.bytes[i]); }
    assert(byte_pipe_read(&p,out,1,&n)==BYTE_PIPE_BROKEN && !n);
    assert(byte_pipe_write(&p,0,0)==BYTE_PIPE_OK && byte_pipe_read(&p,0,0,&n)==BYTE_PIPE_OK && !n);
    assert(byte_pipe_write(&p,0,1)==BYTE_PIPE_ARGUMENT && byte_pipe_write(&p,source,1025)==BYTE_PIPE_ARGUMENT);
    assert(byte_pipe_read(&p,0,1,&n)==BYTE_PIPE_ARGUMENT && !n && byte_pipe_read(&p,out,1,0)==BYTE_PIPE_ARGUMENT);
    puts("PASS: bounded pipe wrap/full/empty, atomic writes, EOF drain, broken endpoints, zeroing and 10000 FIFO cycles.");return 0;
}
