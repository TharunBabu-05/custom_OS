#ifndef OUR_OS_FILE_INPUT_H
#define OUR_OS_FILE_INPUT_H
#include "user.h"

/* Input is a file or the shell's binary stdin. Zero is the stdin sentinel. */
static inline int64_t file_input_open(const char *path)
{ return !path || (path[0]=='-' && !path[1]) ? 0 : uos_open(path,UOS_READ); }

static inline int64_t file_input_read(int64_t input,void *data,size_t bytes)
{ return input ? uos_read((uint64_t)input,data,bytes) : uos_stdin(data,bytes); }

static inline int file_input_finish(int64_t input,int result)
{ return input && uos_close((uint64_t)input) ? 2 : result; }
#endif
