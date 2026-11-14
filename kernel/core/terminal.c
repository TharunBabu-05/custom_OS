#include "terminal.h"
bool terminal_begin(struct terminal_line *l,unsigned capacity)
{
    if (!l) { return false; }*l=(struct terminal_line){0};
    if (capacity<2 || capacity>TERMINAL_BYTES) { return false; }l->capacity=capacity;return true;
}
enum terminal_action terminal_feed(struct terminal_line *l,int ch)
{
    if (!l || !l->capacity || l->done) { return TERM_NONE; }
    if (ch==3) { l->done=true;return TERM_CANCEL; }
    if (ch==4 && !l->length && !l->overflow) { l->done=true;return TERM_EOF; }
    if (ch=='\n' || ch=='\r') { l->done=true;return l->overflow ? TERM_REJECT : TERM_LINE; }
    if (ch==21) { for (unsigned i=0;i<TERMINAL_BYTES;++i) { l->bytes[i]=0; }l->length=0;l->overflow=false;return TERM_CLEAR; }
    if (l->overflow) { return TERM_NONE; }
    if (ch==8 || ch==127) { if (!l->length) { return TERM_NONE; }l->bytes[--l->length]=0;return TERM_ERASE; }
    if (ch<32 || ch>126) { return TERM_NONE; }
    if (l->length==l->capacity-1) { l->overflow=true;return TERM_NONE; }
    l->bytes[l->length++]=(char)ch;l->bytes[l->length]=0;return TERM_ECHO;
}
