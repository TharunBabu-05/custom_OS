#include "terminal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"terminal check failed at %d\n",__LINE__);exit(1); } } while (0)
int main(void)
{
    struct terminal_line l;
    CHECK(!terminal_begin(NULL,4));CHECK(!terminal_begin(&l,1));CHECK(terminal_feed(&l,'x')==TERM_NONE);CHECK(!terminal_begin(&l,129));
    CHECK(terminal_begin(&l,4));CHECK(terminal_feed(&l,'a')==TERM_ECHO);CHECK(terminal_feed(&l,'b')==TERM_ECHO);
    CHECK(terminal_feed(&l,8)==TERM_ERASE && !strcmp(l.bytes,"a"));CHECK(terminal_feed(&l,127)==TERM_ERASE);
    CHECK(terminal_feed(&l,8)==TERM_NONE);CHECK(terminal_feed(&l,4)==TERM_EOF);CHECK(terminal_feed(&l,'x')==TERM_NONE);
    CHECK(terminal_begin(&l,4));terminal_feed(&l,'a');terminal_feed(&l,'b');terminal_feed(&l,'c');
    CHECK(terminal_feed(&l,'d')==TERM_NONE && l.overflow);CHECK(terminal_feed(&l,8)==TERM_NONE);
    CHECK(terminal_feed(&l,'\n')==TERM_REJECT && !strcmp(l.bytes,"abc"));
    CHECK(terminal_begin(&l,4));terminal_feed(&l,'a');terminal_feed(&l,'b');terminal_feed(&l,'c');terminal_feed(&l,'d');
    CHECK(terminal_feed(&l,21)==TERM_CLEAR && !l.length && !l.overflow);
    CHECK(terminal_feed(&l,'z')==TERM_ECHO);CHECK(terminal_feed(&l,4)==TERM_NONE);CHECK(terminal_feed(&l,'\r')==TERM_LINE && !strcmp(l.bytes,"z"));
    CHECK(terminal_begin(&l,128));CHECK(terminal_feed(&l,3)==TERM_CANCEL && l.done);
    unsigned seed=17;
    for (unsigned round=0;round<20000;++round) {
        unsigned cap=2+round%127;CHECK(terminal_begin(&l,cap));
        for (unsigned i=0;i<300;++i) {
            seed=seed*1664525+1013904223;int ch=(int)(seed>>24);struct terminal_line previous=l;
            enum terminal_action a=terminal_feed(&l,ch);CHECK(l.length<cap && l.bytes[l.length]==0 && l.capacity==cap);
            if (previous.done) { CHECK(a==TERM_NONE && !memcmp(&l,&previous,sizeof(l))); }
            if (a==TERM_LINE) { CHECK(!l.overflow && l.done); }if (a==TERM_REJECT) { CHECK(l.overflow && l.done); }
        }
    }
    puts("PASS: terminal editing, clear, overflow rejection, EOF/cancel, frozen completed state and 20000 bounded input traces.");return 0;
}
