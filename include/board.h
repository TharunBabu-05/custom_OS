#ifndef OS_BOARD_H
#define OS_BOARD_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct board_info {
    uintptr_t uart_base;
    uint32_t uart_clock;
    uint64_t ram_base;
    uint64_t ram_size;
    const char *name;
    bool ram_known;
};
bool fdt_read_board(const void *blob, size_t available, struct board_info *out);
bool board_init(struct board_info *info, uintptr_t dtb);
#endif
