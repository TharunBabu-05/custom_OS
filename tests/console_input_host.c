#include <stdio.h>
#include <string.h>
#include "console.h"
#include "host_assert.h"
static const unsigned char *input;
static size_t at,bytes;
static unsigned reads;
int uart_getc(void)
{
    assert(++reads<10000); /* A missing terminator must fail instead of hang. */
    if (reads%3==0) { return -1; } /* Polling gaps must preserve CR/LF state. */
    assert(at<bytes);return input[at++];
}
void uart_putc(char c) { (void)c;assert(!"secret input must never echo"); }
void console_putc(char c) { (void)c;assert(!"secret input must never echo"); }
void console_write(const char *s) { (void)s;assert(!"secret input must never echo"); }
static void feed(const void *p,size_t n) { input=p;bytes=n;at=0;reads=0; }
static int get(void) { int c;do { c=console_getc(); }while(c<0);return c; }
static void wiped(const unsigned char *p,size_t n) { for (size_t i=0;i<n;++i) { assert(!p[i]); } }
static void check(const char *line,const char *expected)
{
    unsigned char out[130];memset(out,0xa5,sizeof(out));size_t n=999;feed(line,strlen(line));
    bool ok=console_read_secret(out,128,&n);assert(ok==(expected!=NULL));
    if (ok) { assert(n==strlen(expected) && !memcmp(out,expected,n));wiped(out+n,128-n); }
    else { assert(!n);wiped(out,128); }
    assert(out[128]==0xa5 && out[129]==0xa5 && get()=='!');
}
int main(void)
{
    /* CRLF command -> hidden password -> CRLF next command: no blank password
     * or password tail becomes a monitor command. These are public fixtures. */
    const char *lines="x\r\nfixture\r\ny\n";feed(lines,strlen(lines));assert(get()=='x' && get()=='\r');
    unsigned char out[128];size_t n;
    assert(console_read_secret(out,128,&n) && n==7 && !memcmp(out,"fixture",7));assert(get()=='y' && get()=='\n');
    check("plain text.\n!","plain text.");check("\r!",NULL);check("\n!",NULL);
    check("ab\bc\177d\n!","ad");check("\b\177ok\n!","ok");check("secret\025new\r\n!","new");
    check("invalid\001tail\n!",NULL);check("cancel\003tail\n!",NULL);check("bad\200tail\n!",NULL);
    check("bad\001\025good\n!","good");
    char line[270];memset(line,'x',128);line[128]='\n';line[129]='!';line[130]=0;
    char expected[129];memset(expected,'x',128);expected[128]=0;check(line,expected);
    memset(line,'x',129);memcpy(line+129,"\b\n!",4);check(line,NULL); /* overflow remains rejected after backspace */
    memset(line,'x',200);memcpy(line+200,"\025fixed\n!",9);check(line,"fixed");
    memset(out,0xa5,sizeof(out));n=99;feed("unused",6);
    assert(!console_read_secret(NULL,128,&n) && !n && !at);
    assert(!console_read_secret(out,0,&n) && !at);assert(!console_read_secret(out,129,&n) && !at);
    assert(!console_read_secret(out,128,NULL) && !at);
    puts("PASS: hidden polling UART input, shared CRLF boundaries, edits/clear/cancel, 128-byte limit, overflow draining, rejection wiping and no echo");return 0;
}
