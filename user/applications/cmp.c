#include "file_input.h"

struct input_buffer {
    int64_t input;
    unsigned char bytes[512];
    size_t at,used;
};

static int refill(struct input_buffer *buffer)
{
    if (buffer->at<buffer->used) { return 0; }
    int64_t n=file_input_read(buffer->input,buffer->bytes,sizeof(buffer->bytes));
    if (n<0) { return 2; }
    buffer->at=0;
    buffer->used=(size_t)n;
    return 0;
}

static int difference(const char *message,uint64_t position)
{
    char digits[21];
    size_t at=sizeof(digits)-1;
    digits[at]='\n';
    do { digits[--at]=(char)('0'+position%10);position/=10; } while (position);
    return uos_puts(message)==(int64_t)uos_length(message) &&
        uos_write(digits+at,sizeof(digits)-at)==(int64_t)(sizeof(digits)-at) ? 1 : 2;
}

static int compare(struct input_buffer *left,struct input_buffer *right)
{
    uint64_t compared=0;
    for (;;) {
        if (refill(left) || refill(right)) { uos_puts("cmp: read failed\n");return 2; }
        size_t a=left->used-left->at,b=right->used-right->at;
        if (!a || !b) { return a==b ? 0 : difference("cmp: length differs at byte ",compared+1); }
        size_t n=a<b ? a : b;
        for (size_t i=0;i<n;++i) {
            if (left->bytes[left->at+i]!=right->bytes[right->at+i]) {
                return difference("cmp: first difference at byte ",compared+i+1);
            }
        }
        compared+=n;
        left->at+=n;
        right->at+=n;
    }
}

int app_main(void)
{
    char a[256],b[256];
    if (uos_argc()!=3 || uos_arg(1,a,sizeof(a))<=0 || uos_arg(2,b,sizeof(b))<=0 ||
        (a[0]=='-' && !a[1] && b[0]=='-' && !b[1])) {
        uos_puts("Usage: cmp <file|-> <file|-> (at most one stdin)\n");return 2;
    }
    struct input_buffer left={.input=file_input_open(a)};
    if (left.input<0) { uos_puts("cmp: cannot open first input\n");return 2; }
    struct input_buffer right={.input=file_input_open(b)};
    if (right.input<0) {
        uos_puts("cmp: cannot open second input\n");return file_input_finish(left.input,2);
    }
    int result=compare(&left,&right);
    result=file_input_finish(right.input,result);
    return file_input_finish(left.input,result);
}
