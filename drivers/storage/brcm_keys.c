/* Original byte serialization of the firmware wsec_key ABI. No C structure
 * layout assumptions. References: brcmfmac fwil_types.h/cfg80211.c. */
#include "brcm_keys.h"
#include "crypto.h"
static void put32(unsigned char *p,unsigned x) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(x>>(8*i)); } }
static bool zero(const unsigned char *p,size_t n) { unsigned x=0;for (size_t i=0;i<n;++i) { x|=p[i]; }return !x; }
enum block_result brcm_key_encode(enum brcm_key_kind kind,unsigned index,const unsigned char peer[6],const unsigned char key[16],const unsigned char seq[6],bool remove,void *out,size_t cap)
{
    if (!out || cap<BRCM_KEY_BYTES) { return BLOCK_ARGUMENT; }
    unsigned char p[BRCM_KEY_BYTES]={0};bool pair=kind==BRCM_KEY_PAIRWISE;
    if ((unsigned)kind>2 || (pair ? index!=0 || !peer || (peer[0]&1) || zero(peer,6) : kind==BRCM_KEY_GROUP ? index>3 : index<4 || index>5) || (!remove && (!key || (!pair && !seq)))) { crypto_wipe(out,BRCM_KEY_BYTES);return BLOCK_ARGUMENT; }
    put32(p,index);put32(p+116,pair ? 0 : 2);
    if (pair) { for (unsigned i=0;i<6;++i) { p[156+i]=peer[i]; } }
    if (!remove) {
        put32(p+4,16);for (unsigned i=0;i<16;++i) { p[8+i]=key[i]; }put32(p+112,4);
        if (!pair) { put32(p+132,1);for (unsigned i=0;i<4;++i) { p[140+i]=seq[2+i]; }p[144]=seq[0];p[145]=seq[1]; }
    }
    for (unsigned i=0;i<BRCM_KEY_BYTES;++i) { ((unsigned char *)out)[i]=p[i]; }crypto_wipe(p,sizeof(p));return BLOCK_OK;
}
enum block_result brcm_keys_install(const struct brcm_key_io *io,const unsigned char peer[6],const unsigned char tk[16],const struct wpa_group_keys *group,struct brcm_key_journal *s)
{
    if (!io || !io->set || !io->permitted || !peer || !tk || !group || !s || group->gtk_id>3 || group->igtk_id<4 || group->igtk_id>5) { return BLOCK_ARGUMENT; }
    if (s->attempted) { return BLOCK_NOT_READY; }*s=(struct brcm_key_journal){.attempted=true,.index={0,group->gtk_id,group->igtk_id}};
    for (unsigned i=0;i<6;++i) { s->peer[i]=peer[i]; }
    enum block_result r=BLOCK_OK;unsigned char wire[BRCM_KEY_BYTES]={0};
    for (unsigned i=0;i<3;++i) {
        s->phase=i+1;if (!io->permitted(io->context)) { r=BLOCK_NOT_READY;break; }
        const unsigned char *key=i==0 ? tk : i==1 ? group->gtk : group->igtk,*seq=i==0 ? NULL : i==1 ? group->rsc : group->ipn;
        r=brcm_key_encode((enum brcm_key_kind)i,s->index[i],peer,key,seq,false,wire,sizeof(wire));if (r!=BLOCK_OK) { break; }
        s->issued_mask|=1u<<i;r=io->set(io->context,wire,sizeof(wire),&s->status);crypto_wipe(wire,sizeof(wire));
        if (r!=BLOCK_OK) { break; }s->ack_mask|=1u<<i;
    }
    crypto_wipe(wire,sizeof(wire));s->error=r;s->installed=r==BLOCK_OK && s->ack_mask==7;return r;
}
enum block_result brcm_keys_clear(const struct brcm_key_io *io,struct brcm_key_journal *s)
{
    if (!io || !io->set || !s) { return BLOCK_ARGUMENT; }if (!s->issued_mask) { s->cleared=true;return BLOCK_OK; }
    if (s->cleared) { return BLOCK_OK; }s->cleanup_active=true;enum block_result first=BLOCK_OK;unsigned char wire[BRCM_KEY_BYTES]={0};
    for (unsigned i=3;i--;) {
        if (!(s->issued_mask&(1u<<i)) || (s->clear_mask&(1u<<i))) { continue; }
        enum block_result r=brcm_key_encode((enum brcm_key_kind)i,s->index[i],s->peer,NULL,NULL,true,wire,sizeof(wire));
        uint32_t status=0;if (r==BLOCK_OK) { r=io->set(io->context,wire,sizeof(wire),&status); }crypto_wipe(wire,sizeof(wire));
        if (r==BLOCK_OK) { s->clear_mask|=1u<<i; }else if (first==BLOCK_OK) { first=r;s->clear_status=status; }
    }
    s->cleanup_active=false;s->clear_error=first;s->cleared=s->clear_mask==s->issued_mask;return first;
}
