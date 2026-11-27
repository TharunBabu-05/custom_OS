/* Shared by the kernel and EL0 runtime. All wide accesses are aligned. */
#include <stdint.h>
#include <string.h>

typedef uint64_t __attribute__((__may_alias__)) memory_word;

void *memcpy(void *destination, const void *source, size_t count)
{
    unsigned char *out = destination;
    const unsigned char *in = source;
    if ((((uintptr_t)out | (uintptr_t)in) & (sizeof(memory_word) - 1)) == 0) {
        while (count >= sizeof(memory_word)) {
            *(memory_word *)out = *(const memory_word *)in;
            out += sizeof(memory_word);
            in += sizeof(memory_word);
            count -= sizeof(memory_word);
        }
    }
    while (count) {
        *out++ = *in++;
        --count;
    }
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    unsigned char *out = destination;
    const unsigned char byte = (unsigned char)value;

    if (((uintptr_t)out & (sizeof(memory_word) - 1)) == 0 && count >= sizeof(memory_word)) {
        const memory_word pattern = (memory_word)byte * UINT64_C(0x0101010101010101);
        while (count >= sizeof(memory_word)) {
            *(memory_word *)out = pattern;
            out += sizeof(memory_word);
            count -= sizeof(memory_word);
        }
    }
    while (count) {
        *out++ = byte;
        --count;
    }
    return destination;
}
