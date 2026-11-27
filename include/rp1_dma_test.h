#ifndef OS_RP1_DMA_TEST_H
#define OS_RP1_DMA_TEST_H
#include <stdbool.h>
#include <stdint.h>

enum { RP1_DMA_TEST_CHANNEL=2, RP1_DMA_TEST_BYTES=64 };
struct rp1_dma_descriptor {
    uint64_t source,destination,blocks,next;
    uint32_t control_low,control_high,unused[6];
};
struct rp1_dma_io {
    void *context;
    bool (*read)(void *,unsigned,uint32_t *);
    bool (*write)(void *,unsigned,uint32_t);
    uint64_t (*ticks)(void *);
    void (*barrier)(void *);
};
enum rp1_dma_result { RP1_DMA_OK,RP1_DMA_BUSY,RP1_DMA_IO,RP1_DMA_TIMEOUT,RP1_DMA_ERROR,RP1_DMA_RETAIN };
struct rp1_dma_report {
    uint32_t status,offset,expected,observed;
    bool register_failed,cleanup_failed;
};
void rp1_dma_descriptor_init(struct rp1_dma_descriptor *,uint64_t source,uint64_t destination);
enum rp1_dma_result rp1_dma_copy(const struct rp1_dma_io *,uint64_t descriptor,uint64_t timeout,struct rp1_dma_report *);
bool rp1_dma_test_command(const char *,uintptr_t dtb,uintptr_t uart);
#ifdef OUROS_PI5
uintptr_t rp1_dma_scratch_address(void);
#endif
#endif
