#ifndef OS_BRCM_KEYS_H
#define OS_BRCM_KEYS_H
#include "block.h"
#include "wpa_kde.h"
#define BRCM_KEY_BYTES 164
enum brcm_key_kind { BRCM_KEY_PAIRWISE,BRCM_KEY_GROUP,BRCM_KEY_INTEGRITY };
struct brcm_key_io {
    void *context;
    enum block_result (*set)(void *,const void *,size_t,uint32_t *);
    bool (*permitted)(void *);
};
/* Journal contains identifiers/status only, NEVER keys or packet counters. */
struct brcm_key_journal {
    bool attempted,installed,cleanup_active,cleared;
    unsigned issued_mask,ack_mask,clear_mask,phase,index[3];unsigned char peer[6];
    enum block_result error,clear_error;uint32_t status,clear_status;
};
enum block_result brcm_key_encode(enum brcm_key_kind,unsigned,const unsigned char peer[6],const unsigned char key[16],const unsigned char sequence[6],bool remove,void *,size_t);
static inline unsigned brcm_key_le32(const unsigned char *p) { return (unsigned)p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24; }
static inline bool brcm_key_iszero(const unsigned char *p,size_t n) { unsigned x=0;for (size_t i=0;i<n;++i) { x|=p[i]; }return !x; }
static inline bool brcm_key_wire_valid(const void *buffer,size_t n)
{
    if (!buffer || n!=BRCM_KEY_BYTES) { return false; }const unsigned char *p=buffer;unsigned index=brcm_key_le32(p),len=brcm_key_le32(p+4),flags=brcm_key_le32(p+116);
    bool pair=flags==0;if (flags!=0 && flags!=2) { return false; }
    if (pair ? index!=0 || brcm_key_iszero(p+156,6) || (p[156]&1) : index>5 || !brcm_key_iszero(p+156,6)) { return false; }
    if (!brcm_key_iszero(p+40,72) || !brcm_key_iszero(p+120,12) || !brcm_key_iszero(p+136,4) || !brcm_key_iszero(p+146,10) || !brcm_key_iszero(p+162,2)) { return false; }
    if (!len) { return brcm_key_le32(p+112)==0 && brcm_key_iszero(p+8,32) && !brcm_key_le32(p+132) && brcm_key_iszero(p+140,6); }
    return len==16 && brcm_key_le32(p+112)==4 && brcm_key_iszero(p+24,16) && (pair ? !brcm_key_le32(p+132) && brcm_key_iszero(p+140,6) : brcm_key_le32(p+132)==1);
}

enum block_result brcm_keys_install(const struct brcm_key_io *,const unsigned char peer[6],const unsigned char tk[16],const struct wpa_group_keys *,struct brcm_key_journal *);
enum block_result brcm_keys_clear(const struct brcm_key_io *,struct brcm_key_journal *);
#endif
