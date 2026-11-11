#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "host_assert.h"
#include "brcm_keys.h"
#include "brcm_control.h"
/* Independent ABI layout model; public bytes only. */
struct reference_key { uint32_t index,len;unsigned char data[32];uint32_t pad1[18],algo,flags,pad2[3],iv,pad3;struct { uint32_t hi;uint16_t lo; } rx;uint32_t pad4[2];unsigned char peer[6]; };
_Static_assert(sizeof(struct reference_key)==164 && offsetof(struct reference_key,algo)==112 && offsetof(struct reference_key,peer)==156,"firmware ABI");
static struct brcm_control_state s;
static struct { unsigned calls,fail_at,writes,position,bytes;bool pending,cancel;unsigned char reply[256]; } m;
static enum block_result touch(void) { return ++m.calls==m.fail_at ? BLOCK_IO : BLOCK_OK; }
static void put32(unsigned char *p,unsigned x) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(x>>(8*i)); } }
static enum block_result rd(void *ctx,uint32_t at,uint32_t *v) { assert(ctx==&m);*v=at==0x18004020 && m.pending ? 0x40 : 0;return touch(); }
static enum block_result wr(void *ctx,uint32_t at,uint32_t v) { assert(ctx==&m && at==0x18004020 && v==0x40);return touch(); }
static enum block_result delay(void *ctx,unsigned us) { assert(ctx==&m && us==10000);return touch(); }
static bool permit(void *ctx) { assert(ctx==&m);return !m.cancel; }
static enum block_result packet(void *ctx,bool write,void *buffer,size_t n)
{
    assert(ctx==&m);enum block_result r=touch();if (r!=BLOCK_OK) { return r; }unsigned char *p=buffer;
    if (write) {
        ++m.writes;assert(n==204 && brcm_key_le32(p+12)==263 && brcm_key_le32(p+16)==173 && !memcmp(p+28,"wsec_key",9) && brcm_key_wire_valid(p+37,164));
        memcpy(m.reply,p,n);m.reply[4]=s.rx_seq;m.reply[9]=(unsigned char)(s.tx_seq+8);put32(m.reply+24,0);m.bytes=(unsigned)n;m.position=0;m.pending=true;return BLOCK_OK;
    }
    assert(m.pending && m.position+n<=m.bytes);memcpy(p,m.reply+m.position,n);m.position+=(unsigned)n;if (m.position==m.bytes) { m.pending=false; }return BLOCK_OK;
}
static const struct brcm_control_io io={.context=&m,.read32=rd,.write32=wr,.packet=packet,.delay_us=delay};
static void seed(void) { memset(&s,0,sizeof(s));memset(&m,0,sizeof(m));s.ready=s.f2_ready=s.device_ready=true;s.tx_seq=1;s.tx_max=5;memset(s.frame,0xa5,sizeof(s.frame));memset(s.response,0xa5,sizeof(s.response));memset(s.glom,0xa5,sizeof(s.glom));memset(s.glom_subheader,0xa5,sizeof(s.glom_subheader)); }
static void erased(const void *p,size_t n) { const unsigned char *b=p;for (size_t i=0;i<n;++i) { assert(!b[i]); } }
static struct { unsigned calls,fail_at,clears;bool cancel; } jmodel;
static bool jpermit(void *ctx) { assert(ctx==&jmodel);return !jmodel.cancel; }
static enum block_result jset(void *ctx,const void *wire,size_t n,uint32_t *status)
{ assert(ctx==&jmodel && brcm_key_wire_valid(wire,n));*status=0;++jmodel.calls;if (!brcm_key_le32((const unsigned char *)wire+4)) { ++jmodel.clears; }if (jmodel.calls==jmodel.fail_at) { *status=0xffffffe9u;return BLOCK_IO; }return BLOCK_OK; }
int main(void)
{
    const unsigned char peer[6]={2,0,0,0,0,2},sequence[6]={1,2,3,4,5,6};unsigned char key[16],wire[165];for (unsigned i=0;i<16;++i) { key[i]=(unsigned char)(i+1); }
    for (unsigned kind=0;kind<3;++kind) { for (unsigned remove=0;remove<2;++remove) {
        unsigned index=kind==0 ? 0 : kind==1 ? 1 : 4;memset(wire,0xa5,sizeof(wire));
        assert(brcm_key_encode((enum brcm_key_kind)kind,index,peer,key,sequence,remove,wire,164)==BLOCK_OK && wire[164]==0xa5 && brcm_key_wire_valid(wire,164));
        struct reference_key expected={.index=index,.flags=kind ? 2 : 0};if (!kind) { memcpy(expected.peer,peer,6); }
        if (!remove) { expected.len=16;expected.algo=4;memcpy(expected.data,key,16);if (kind) { expected.iv=1;expected.rx.hi=0x06050403;expected.rx.lo=0x0201; } }
        assert(!memcmp(wire,&expected,164));
    } }
    assert(brcm_key_encode(BRCM_KEY_PAIRWISE,0,peer,key,NULL,false,wire,164)==BLOCK_OK);seed();uint32_t status=0;
    assert(brcm_control_key(&io,&s,wire,164,&status,permit,&m)==BLOCK_OK && m.writes==1 && !s.waiting && s.ready);erased(s.frame,sizeof(s.frame));erased(s.response,sizeof(s.response));erased(s.glom,sizeof(s.glom));erased(s.glom_subheader,sizeof(s.glom_subheader));unsigned total=m.calls;
    for (unsigned fail=1;fail<=total;++fail) { seed();m.fail_at=fail;assert(brcm_control_key(&io,&s,wire,164,&status,permit,&m)!=BLOCK_OK && !s.ready);erased(s.frame,sizeof(s.frame));erased(s.response,sizeof(s.response)); }
    seed();m.cancel=true;assert(brcm_control_key(&io,&s,wire,164,&status,permit,&m)==BLOCK_NOT_READY && !m.writes && s.ready);erased(s.frame,sizeof(s.frame));erased(s.response,sizeof(s.response));
    seed();wire[40]=1;assert(brcm_control_key(&io,&s,wire,164,&status,permit,&m)==BLOCK_ARGUMENT && !m.calls);wire[40]=0;
    size_t actual=0;assert(brcm_control_iovar(&io,&s,true,"wsec_key",wire,164,NULL,0,&actual,&status)==BLOCK_UNSUPPORTED && !m.calls);
    struct wpa_group_keys group={.gtk_id=1,.igtk_id=4};memcpy(group.gtk,key,16);memcpy(group.igtk,key,16);memcpy(group.rsc,sequence,6);memcpy(group.ipn,sequence,6);
    const struct brcm_key_io kio={&jmodel,jset,jpermit};struct brcm_key_journal journal;
    for (unsigned fail=0;fail<=6;++fail) {
        memset(&journal,0,sizeof(journal));memset(&jmodel,0,sizeof(jmodel));jmodel.fail_at=fail;
        enum block_result r=brcm_keys_install(&kio,peer,key,&group,&journal);assert((r==BLOCK_OK)==(fail==0 || fail>3));
        assert(brcm_keys_install(&kio,peer,key,&group,&journal)==BLOCK_NOT_READY);
        r=brcm_keys_clear(&kio,&journal);assert(journal.cleared==(fail<=3));assert((r==BLOCK_OK)==journal.cleared);
        if (fail<=3) { assert(journal.clear_mask==journal.issued_mask); }else { assert(journal.clear_error==BLOCK_IO && journal.clear_status==0xffffffe9u); }
    }
    puts("PASS: independent 164-byte firmware key ABI/offsets, PTK/GTK/IGTK and counter encoding, exact guarded CDC SET with echo replies, all transport failures and shared-buffer erasure, typed-only access, partial-install/delete journal and no repeated install");return 0;
}
