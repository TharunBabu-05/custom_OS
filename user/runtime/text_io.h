#ifndef OUROS_TEXT_IO_H
#define OUROS_TEXT_IO_H
#include "file_input.h"
#include <stdbool.h>

enum { TEXT_IO_BYTES=512, TEXT_LINE_BYTES=4096 };
struct text_reader {
    int64_t input;
    size_t at,used;
    unsigned char buffer[TEXT_IO_BYTES];
};

bool text_count(const char *text,unsigned limit,unsigned *count);
int64_t text_open_input(unsigned path_arg);
bool text_write(const void *data,size_t length);
int text_compare(const unsigned char *left,size_t left_bytes,const unsigned char *right,size_t right_bytes);
/* Returns 1 for a record, 0 at EOF, -1 on I/O error, and -2 for an oversized line.
 * The returned payload excludes LF. A final unterminated record is retained. */
int text_read_line(struct text_reader *,unsigned char line[TEXT_LINE_BYTES],size_t *bytes,bool *newline);
int text_line_error(const char *name,int result);
#endif
