#include "rng200.h"
#include "dt_property.h"
#include "crypto.h"
#include "console.h"
#include "cpu.h"
#ifdef OUROS_PI5
static struct rng200_state source;
static uintptr_t base;
static bool tested;
static bool test_pass;
#endif
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static bool bytes(const unsigned char *p,uint32_t n,const char *s)
{ uint32_t i=0;while (s[i]) { if (i>=n || p[i]!=(unsigned char)s[i]) { return false; }++i; }return i<n && !p[i]; }
bool pi5_rng200_description(const void *dt,size_t available,uint64_t *address)
{
    const char *soc="/soc@107c000000",*rng="/soc@107c000000/rng@7d208000";
    const unsigned char *p;uint32_t n;
    if (!address || !dt_property(dt,available,"/","compatible",&p,&n)) { return false; }
    bool board=false;for (uint32_t at=0;at<n;) { if (bytes(p+at,n-at,"brcm,bcm2712")) { board=true; }while (at<n && p[at]) { ++at; }++at; }if (!board) { return false; }
    const char *paths[]={"/","/",soc,soc};const char *keys[]={"#address-cells","#size-cells","#address-cells","#size-cells"};
    for (unsigned i=0;i<4;++i) { if (!dt_property(dt,available,paths[i],keys[i],&p,&n) || n!=4 || be32(p)!=(i<2 ? 2u : 1u)) { return false; } }
    if (!dt_property(dt,available,soc,"ranges",&p,&n) || n!=16 || be32(p)!=0 || be32(p+4)!=16 || be32(p+8)!=0 || be32(p+12)!=0x80000000) { return false; }
    if (!dt_property(dt,available,rng,"compatible",&p,&n) || !bytes(p,n,"brcm,bcm2711-rng200")) { return false; }
    if (!dt_property(dt,available,rng,"status",&p,&n) || !bytes(p,n,"okay")) { return false; }
    if (!dt_property(dt,available,rng,"reg",&p,&n) || n!=8 || be32(p)!=0x7d208000 || be32(p+4)!=0x28) { return false; }
    *address=0x107d208000ULL;return true;
}
#ifdef OUROS_PI5
static bool read_reg(void *ctx,unsigned at,uint32_t *v)
{ (void)ctx;if ((at&3) || at>=0x28) { return false; }*v=*(volatile uint32_t *)(base+at);__asm__ volatile("dmb sy" ::: "memory");return true; }
static bool write_reg(void *ctx,unsigned at,uint32_t v)
{
    (void)ctx;if (!((at==0x10 && v==0x40000) || (at==0x24 && v==512) || at==0)) { return false; }
    __asm__ volatile("dmb sy" ::: "memory");*(volatile uint32_t *)(base+at)=v;__asm__ volatile("dsb sy" ::: "memory");return true;
}
static uint64_t ticks(void *ctx) { (void)ctx;return timer_ticks(); }
enum block_result platform_random_bytes(void *out,size_t n) { return rng200_bytes(&source,out,n); }
void platform_rng_test(uintptr_t dtb)
{
    if (!tested) {
        uint64_t address;if (!pi5_rng200_description((const void *)dtb,0x200000,&address)) { console_write("RNG200: unsupported DT mapping; no MMIO touched.\n");return; }
        base=(uintptr_t)address;tested=true;struct rng200_io io={NULL,read_reg,write_reg,ticks,timer_frequency()};
        enum block_result r=rng200_init(&io,&source);unsigned char first[64]={0},second[64]={0};
        if (r==BLOCK_OK) { r=rng200_bytes(&source,first,sizeof(first)); }
        if (r==BLOCK_OK) { r=rng200_bytes(&source,second,sizeof(second)); }
        if (r==BLOCK_OK && crypto_equal(first,second,sizeof(first))) { source.ready=false;source.error=BLOCK_IO; }
        test_pass=r==BLOCK_OK && source.ready;crypto_wipe(first,sizeof(first));crypto_wipe(second,sizeof(second));
    }else { console_write("RNG self-test already attempted; cached status only.\n"); }
    console_write("RNG200: ");console_write(block_error(source.error));console_write(" phase=");console_u64(source.phase);console_write(" base=");console_hex(base);
    console_write(source.inherited ? " configuration=inherited\n" : " configuration=initialized\n");
    console_write("control=");console_hex(source.control);console_write(" status=");console_hex(source.status);console_write(" warmup_bits=");console_u64(source.total_bits);
    console_write(" discarded_words=");console_u64(source.discarded);console_write(" consumed_words=");console_u64(source.words);console_write(" conditioned_blocks=");console_u64(source.blocks);console_write("\n");
    console_write(test_pass ? "PASS: hardware source supplied two distinct 64-byte conditioned samples; sample bytes erased.\n" : "RNG test failed; randomness unavailable. Reboot before retry.\n");
    console_write("No random bytes printed. Engineering health checks are not entropy certification; RNG testing alone does not authenticate Wi-Fi.\n");
}
#else
enum block_result platform_random_bytes(void *out,size_t n) { if (out && n<=256) { crypto_wipe(out,n); }return BLOCK_NOT_READY; }
void platform_rng_test(uintptr_t dtb) { (void)dtb;console_write("No Pi RNG200 source on QEMU virt; randomness unavailable.\n"); }
#endif
