#ifndef OS_TERMINAL_H
#define OS_TERMINAL_H
#include <stdbool.h>
enum { TERMINAL_BYTES=128 };
enum terminal_action { TERM_NONE,TERM_ECHO,TERM_ERASE,TERM_CLEAR,TERM_LINE,TERM_REJECT,TERM_CANCEL,TERM_EOF };
struct terminal_line { char bytes[TERMINAL_BYTES];unsigned length,capacity;bool overflow,done; };
bool terminal_begin(struct terminal_line *,unsigned capacity);
enum terminal_action terminal_feed(struct terminal_line *,int ch);
#endif
