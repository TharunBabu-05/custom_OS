#ifndef OS_RNG200_H
#define OS_RNG200_H
#include "block.h"
struct rng200_io {
    void *context;
    bool (*read)(void *,unsigned,uint32_t *);
    bool (*write)(void *,unsigned,uint32_t);
    uint64_t (*ticks)(void *);
    uint64_t hz;
};
struct rng200_state {
    struct rng200_io io;
    enum block_result error;
    bool attempted,ready,inherited,changed,have_previous;
    unsigned phase,discarded,words,blocks;
    uint32_t control,status,total_bits,fifo_count;
    uint32_t previous; /* Internal health-check state; never print. */
};
bool pi5_rng200_description(const void *,size_t,uint64_t *address);
enum block_result rng200_init(const struct rng200_io *,struct rng200_state *);
/* Fresh hardware words per output block, hashed 64->32 bytes. No seeded PRNG
 * fallback. Success depends on the hardware source; engineering checks do
 * not certify entropy or constitute a NIST 90B validation. max 256 bytes. */
enum block_result rng200_bytes(struct rng200_state *,void *,size_t);
void platform_rng_test(uintptr_t dtb);
enum block_result platform_random_bytes(void *,size_t);
#endif
