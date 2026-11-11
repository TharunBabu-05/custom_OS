/* Reversible firmware policy/event-mask check while the interface is DOWN.
 * No association, radio-UP, management-frame send or credential API is used. */
#include "brcm_auth.h"
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put32(unsigned char *p,uint32_t v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(8*i)); } }
static bool equal(const unsigned char *a,const unsigned char *b,size_t n)
{ for (size_t i=0;i<n;++i) { if (a[i]!=b[i]) { return false; } }return true; }
static const char *const names[]={"auth","wpa_auth","wsec","mfp"};
static const uint32_t requested[]={3,0x40000,4,2};
static enum block_result get(const struct brcm_auth_probe_io *io,unsigned i,uint32_t *value,uint32_t *status)
{
    unsigned char data[4];size_t n;enum block_result r=io->iovar(io->context,false,names[i],NULL,0,data,4,&n,status);
    if (r!=BLOCK_OK) { return r; }if (n!=4) { return BLOCK_RANGE; }*value=le32(data);return BLOCK_OK;
}
static enum block_result set(const struct brcm_auth_probe_io *io,unsigned i,uint32_t value,uint32_t *status)
{ unsigned char data[4];size_t n;put32(data,value);return io->iovar(io->context,true,names[i],data,4,NULL,0,&n,status); }
static enum block_result mask_get(const struct brcm_auth_probe_io *io,unsigned char mask[BRCM_AUTH_MASK_MAX],unsigned *bytes,uint32_t *status,struct brcm_auth_probe *s)
{
    unsigned char request[4+BRCM_AUTH_MASK_MAX]={1,0,BRCM_AUTH_MASK_MAX,BRCM_AUTH_MASK_MAX},data[4+BRCM_AUTH_MASK_MAX]={0};size_t n=0;
    s->property=5;s->mask_legacy_verified=false;
    enum block_result r=io->iovar(io->context,false,"event_msgs_ext",request,sizeof(request),data,sizeof(data),&n,status);
    s->mask_reply_bytes=(unsigned)n;
    for (unsigned i=0;i<4;++i) { s->mask_header[i]=i<n ? data[i] : 0; }
    if (r!=BLOCK_OK) { return r; }
    /* Never set a partial snapshot: preserve every bit advertised by firmware,
     * including events beyond those this kernel currently understands. */
    if (n>sizeof(data) || n<4 || data[2]<24 || n<4u+data[2]) { return BLOCK_UNSUPPORTED; }
    /* The pinned 7.45.265 firmware was observed returning {'e','v',24,24}:
     * consistent with retained IOVAR-name bytes in its GET buffer. Reference
     * DHD GET callers use len/mask and construct a fresh v1 header for SET.
     * Accept only this captured non-v1 form, never arbitrary unknown versions.
     * Cross-check the overlapping 128 bits using the independently proven API. */
    bool compatibility=data[0]=='e' && data[1]=='v' && data[2]==24 && data[3]==24;
    if (data[0]!=1 && !compatibility) { return BLOCK_UNSUPPORTED; }
    s->mask_compatibility|=compatibility;
    unsigned char legacy[16];size_t legacy_bytes=0;s->property=6;
    r=io->iovar(io->context,false,"event_msgs",NULL,0,legacy,sizeof(legacy),&legacy_bytes,status);
    if (r!=BLOCK_OK) { return r; }
    if (legacy_bytes!=sizeof(legacy) || !equal(legacy,data+4,sizeof(legacy))) { return BLOCK_IO; }
    s->mask_legacy_verified=true;s->property=5;
    *bytes=data[2];for (unsigned i=0;i<*bytes;++i) { mask[i]=data[4+i]; }return BLOCK_OK;
}
static enum block_result mask_set(const struct brcm_auth_probe_io *io,const unsigned char mask[BRCM_AUTH_MASK_MAX],unsigned bytes,uint32_t *status)
{
    unsigned char request[4+BRCM_AUTH_MASK_MAX]={1,3,0,0};size_t n;request[2]=(unsigned char)bytes;
    for (unsigned i=0;i<bytes;++i) { request[4+i]=mask[i]; }
    return io->iovar(io->context,true,"event_msgs_ext",request,4+bytes,NULL,0,&n,status);
}
enum block_result brcm_auth_policy_events(const struct brcm_auth_probe_io *io,bool prerequisites,struct brcm_auth_probe *s,uint32_t extra_events,enum block_result (*action)(void *),void *context)
{
    if (!io || !io->iovar || !s) { return BLOCK_ARGUMENT; }
    if (s->attempted) { return s->error; }if (!prerequisites) { return BLOCK_NOT_READY; }
    *s=(struct brcm_auth_probe){.attempted=true,.phase=1,.error=BLOCK_NOT_READY,.extra_events=extra_events};enum block_result r=BLOCK_OK;
    for (unsigned i=0;i<4;++i) { s->property=i+1;r=get(io,i,&s->saved[i],&s->firmware_status);if (r!=BLOCK_OK) { goto done; } }
    s->phase=2;s->property=5;r=mask_get(io,s->mask,&s->mask_bytes,&s->firmware_status,s);if (r!=BLOCK_OK) { goto done; }
    unsigned char current[BRCM_AUTH_MASK_MAX];unsigned bytes;
    r=mask_get(io,current,&bytes,&s->firmware_status,s);if (r!=BLOCK_OK) { goto done; }
    if (bytes!=s->mask_bytes || !equal(current,s->mask,bytes)) { r=BLOCK_IO;goto done; }
    s->mask_snapshot_stable=true;
    s->phase=3;s->changed=true;
    /* brcmfmac selects WPA mode before SAE auth, then cipher/PMF. A firmware
     * WPA-mode transition may reset auth; the Pi capture does not establish
     * which write caused its final auth=2, so record all four boundaries. */
    static const unsigned apply_order[]={1,0,2,3};
    for (unsigned step=0;step<4;++step) {
        unsigned i=apply_order[step];s->property=i+1;
        r=set(io,i,requested[i],&s->firmware_status);if (r!=BLOCK_OK) { goto restore; }
        s->property=1;r=get(io,0,&s->auth_after[i],&s->firmware_status);if (r!=BLOCK_OK) { goto restore; }
        s->auth_trace_mask|=1u<<i;
    }
    unsigned char enabled[BRCM_AUTH_MASK_MAX];for (unsigned i=0;i<BRCM_AUTH_MASK_MAX;++i) { enabled[i]=s->mask[i]; }enabled[23]|=0x78;
    for (unsigned i=0;i<4;++i) { enabled[i]|=(unsigned char)(extra_events>>(8*i)); }
    s->property=5;r=mask_set(io,enabled,s->mask_bytes,&s->firmware_status);if (r!=BLOCK_OK) { goto restore; }
    s->phase=4;
    for (unsigned i=0;i<4;++i) {
        s->property=i+1;r=get(io,i,&s->observed[i],&s->firmware_status);if (r!=BLOCK_OK) { goto restore; }
        s->observed_mask|=1u<<i;
        /* wsec combines cipher bits and MFP flags. Accept AES alone or AES
         * with both CAPABLE/REQUIRED, paired with the separate strict mfp=2
         * check. Unknown, weaker, partial-MFP or extra cipher bits fail. */
        bool match=s->observed[i]==requested[i] || (i==2 && s->observed[i]==0x604);
        if (!match) { s->mismatch_mask|=1u<<i; }
    }
    s->property=5;r=mask_get(io,current,&bytes,&s->firmware_status,s);if (r!=BLOCK_OK) { goto restore; }
    if (bytes!=s->mask_bytes || !equal(enabled,current,bytes)) { r=BLOCK_IO;goto restore; }
    s->mask_readback_verified=true;
    s->auth_readback_pending=s->observed[0]==2;
    bool request_probe_only=action && s->mismatch_mask==1 && s->auth_readback_pending;
    if (s->mismatch_mask && !request_probe_only) {
        for (unsigned i=0;i<4;++i) { if (s->mismatch_mask&(1u<<i)) { s->property=i+1;break; } }
        r=BLOCK_IO;goto restore;
    }
    if (action) {
        s->phase=7;s->property=7;r=action(context);if (r!=BLOCK_OK) { goto restore; }
        s->external_request_verified=true;
    }
    s->verified=true;s->phase=5;
restore:;
    if (r!=BLOCK_OK) { s->failure_phase=s->phase;s->failure_property=s->property; }
    /* Restore WPA mode before auth, and wsec before mfp. Retain the first
     * restoration failure and its firmware status even if later reads pass. */
    static const unsigned order[]={2,1,0,3};enum block_result restored=BLOCK_OK,step;uint32_t status;
    for (unsigned i=0;i<4;++i) {
        status=0;step=set(io,order[i],s->saved[order[i]],&status);
        if (restored==BLOCK_OK && step!=BLOCK_OK) { restored=step;s->restore_property=order[i]+1;s->restore_status=status; }
    }
    status=0;step=mask_set(io,s->mask,s->mask_bytes,&status);
    if (restored==BLOCK_OK && step!=BLOCK_OK) { restored=step;s->restore_property=5;s->restore_status=status; }
    for (unsigned i=0;i<4;++i) {
        uint32_t value=0;status=0;step=get(io,i,&value,&status);
        if (step==BLOCK_OK && value!=s->saved[i]) { step=BLOCK_IO; }
        if (restored==BLOCK_OK && step!=BLOCK_OK) { restored=step;s->restore_property=i+1;s->restore_status=status; }
    }
    status=0;step=mask_get(io,current,&bytes,&status,s);
    if (step==BLOCK_OK && (bytes!=s->mask_bytes || !equal(current,s->mask,bytes))) { step=BLOCK_IO; }
    if (restored==BLOCK_OK && step!=BLOCK_OK) { restored=step;s->restore_property=s->property;s->restore_status=status; }
    s->restore_error=restored;s->restored=restored==BLOCK_OK;if (r==BLOCK_OK && restored!=BLOCK_OK) { r=restored; }
    if (r==BLOCK_OK) { s->phase=6; }
done:
    if (r!=BLOCK_OK) {
        if (s->failure_phase) { s->phase=s->failure_phase;s->property=s->failure_property; }
        else if (s->restore_error!=BLOCK_OK) { s->phase=5;s->property=s->restore_property; }
    }
    s->error=r;return r;
}
enum block_result brcm_auth_probe_run(const struct brcm_auth_probe_io *io,bool prerequisites,struct brcm_auth_probe *s)
{ return brcm_auth_policy_action(io,prerequisites,s,NULL,NULL); }
enum block_result brcm_auth_policy_action(const struct brcm_auth_probe_io *io,bool prerequisites,struct brcm_auth_probe *s,enum block_result (*action)(void *),void *context)
{ return brcm_auth_policy_events(io,prerequisites,s,0,action,context); }
