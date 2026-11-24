#ifndef OS_RP1_ETH_LOOP_H
#define OS_RP1_ETH_LOOP_H
#include "ethernet_ipv4.h"
#include "rp1_dma_test.h"
#include "udp.h"
#include "tcp.h"

enum { GEM_FRAME_BYTES=96, GEM_RX_BYTES=1536, GEM_GUARD_BYTES=64 };
struct gem_descriptor { uint32_t address,status; };
struct rp1_eth_loop_memory {
    _Alignas(64) struct gem_descriptor tx[2];
    _Alignas(64) struct gem_descriptor rx[2];
    _Alignas(64) uint8_t frame[GEM_FRAME_BYTES];
    _Alignas(64) uint8_t before[GEM_GUARD_BYTES], received[GEM_RX_BYTES], after[GEM_GUARD_BYTES];
};
enum rp1_eth_loop_result { GEM_LOOP_OK,GEM_LOOP_BUSY,GEM_LOOP_SETUP,GEM_LOOP_FRAME,GEM_LOOP_RETAIN };
struct rp1_eth_loop_report {
    uint32_t queues,mask,tx,rx,tsr,rsr;
    uint64_t peer_mac;
    uint32_t offset,expected,observed;
    bool register_failed,echo;
};
/* Zero speed selects internal loopback. Other speeds send a test ARP request. */
enum rp1_eth_loop_result rp1_eth_exchange(const struct rp1_dma_io *,volatile struct rp1_eth_loop_memory *,
                                        uint64_t bus,uint64_t timeout,unsigned speed,struct rp1_eth_loop_report *);
enum rp1_eth_loop_result rp1_eth_ping(const struct rp1_dma_io *,volatile struct rp1_eth_loop_memory *,
                                    uint64_t bus,uint64_t timeout,unsigned speed,struct rp1_eth_loop_report *);

struct gem_registers {
    uint32_t cfg,dma,tx_base,rx_base,tx_status,rx_status,sa_low,sa_high;
};
struct rp1_eth_dma_memory {
    _Alignas(64) volatile struct gem_descriptor tx,rx[2];
    _Alignas(64) uint8_t frame[TCP_FRAME_MAX];
    _Alignas(64) uint8_t received[2][GEM_RX_BYTES];
};
struct rp1_eth_dma {
    const struct rp1_dma_io *io;
    struct rp1_eth_dma_memory *memory;
    struct gem_registers saved;
    struct ethernet_ipv4 ipv4;
    uint64_t submitted,reply_due[2];
    uint32_t bus;
    unsigned head;
    bool pending,tcp_enabled,tcp_first;
};
extern const uint8_t rp1_eth_mac[6];
extern const struct ethernet_ipv4 rp1_eth_default_ipv4;
enum rp1_eth_loop_result rp1_eth_dma_open(struct rp1_eth_dma *,const struct rp1_dma_io *,
                                         struct rp1_eth_dma_memory *,uint32_t bus,unsigned speed,
                                         const struct ethernet_ipv4 *,bool tcp_enabled);
enum block_result rp1_eth_dma_service(struct rp1_eth_dma *,uint64_t now_ms);
bool rp1_eth_dma_close(struct rp1_eth_dma *);
#endif
