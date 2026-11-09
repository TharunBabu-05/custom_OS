/* Original passive escan/event parser. Layout definitions are documented in
 * docs/wifi-scan.md. RF-discovered names/IEs are untrusted bounded byte arrays. */
#include "brcm_scan.h"
static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0]|(unsigned)p[1]<<8); }
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint16_t be16(const unsigned char *p) { return (uint16_t)((unsigned)p[0]<<8|p[1]); }
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static void put16(unsigned char *p,uint16_t v) { p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8); }
static void put32(unsigned char *p,uint32_t v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(8*i)); } }
static bool equal(const unsigned char *a,const unsigned char *b,size_t n)
{ for (size_t i=0;i<n;++i) { if (a[i]!=b[i]) { return false; } }return true; }
static bool suite(const unsigned char *p,unsigned type)
{ return !p[0] && p[1]==15 && p[2]==172 && p[3]==type; }
static enum block_result rsn(const unsigned char *p,size_t n,struct brcm_scan_ap *ap)
{
    if (n<8 || le16(p)!=1) { return BLOCK_RANGE; }size_t at=6;unsigned count=le16(p+at);at+=2;
    if (!count || count>(n-at)/4) { return BLOCK_RANGE; }at+=count*4;
    if (n-at<2) { return BLOCK_RANGE; }count=le16(p+at);at+=2;
    if (!count || count>(n-at)/4) { return BLOCK_RANGE; }
    for (unsigned i=0;i<count;++i,at+=4) {
        if (suite(p+at,2) || suite(p+at,4) || suite(p+at,6)) { ap->security|=SCAN_PSK; }
        else if (suite(p+at,8) || suite(p+at,9)) { ap->security|=SCAN_SAE; }
        else if (suite(p+at,18)) { ap->security|=SCAN_OWE; }
        else if (suite(p+at,1) || suite(p+at,3) || suite(p+at,5) || suite(p+at,11) || suite(p+at,12) || suite(p+at,13)) { ap->security|=SCAN_ENTERPRISE; }
        else { ap->security|=SCAN_OTHER_AKM; }
    }
    if (at==n) { return BLOCK_OK; }if (n-at<2) { return BLOCK_RANGE; }
    unsigned caps=le16(p+at);at+=2;ap->pmf_capable=!!(caps&128);ap->pmf_required=!!(caps&64);
    if (ap->pmf_required && !ap->pmf_capable) { return BLOCK_RANGE; }
    if (at==n) { return BLOCK_OK; }if (n-at<2) { return BLOCK_RANGE; }
    count=le16(p+at);at+=2;if (count>(n-at)/16) { return BLOCK_RANGE; }at+=count*16;
    return n-at==0 || n-at==4 ? BLOCK_OK : BLOCK_RANGE;
}
enum block_result brcm_scan_bss(const void *buffer,size_t n,struct brcm_scan_ap *ap)
{
    if (!buffer || !ap) { return BLOCK_ARGUMENT; }const unsigned char *p=buffer;
    if (n<126 || le32(p)!=109) { return BLOCK_UNSUPPORTED; }
    if (le32(p+4)!=n || p[18]>32) { return BLOCK_RANGE; }
    uint32_t ie_at=le16(p+116),ie_bytes=le32(p+120);
    if (ie_at<126 || ie_at>n || ie_bytes>n-ie_at) { return BLOCK_RANGE; }
    *ap=(struct brcm_scan_ap){0};unsigned nonzero=0;
    for (unsigned i=0;i<6;++i) { ap->bssid[i]=p[8+i];nonzero|=ap->bssid[i]; }
    if (!nonzero || (ap->bssid[0]&1)) { return BLOCK_RANGE; }
    ap->ssid_length=p[18];for (unsigned i=0;i<ap->ssid_length;++i) { ap->ssid[i]=p[19+i]; }
    unsigned raw_rssi=le16(p+78);int rssi=raw_rssi>=32768 ? (int)raw_rssi-65536 : (int)raw_rssi;
    ap->rssi=(int16_t)(rssi>=-127 && rssi<=0 ? rssi : -128);ap->chanspec=le16(p+72);
    unsigned band=ap->chanspec&0xc000;ap->band=band==0 ? 2 : band==0xc000 ? 5 : 0;
    if (!ap->band) { return BLOCK_UNSUPPORTED; }
    int channel=p[88];
    if (!channel) {
        channel=ap->chanspec&255;unsigned bw=ap->chanspec&0x3800,sb=(ap->chanspec>>8)&7;
        if (bw==0x1000 && !sb) { /* 20 MHz */ }
        else if (bw==0x1800 && sb<2) { channel+=(int)sb*4-2; }
        else if (bw==0x2000 && sb<4) { channel+=(int)sb*4-6; }
        else if (bw==0x2800 && sb<8) { channel+=(int)sb*4-14; }
        else { return BLOCK_UNSUPPORTED; }
    }
    if (channel<1 || channel>(ap->band==2 ? 14 : 196)) { return BLOCK_RANGE; }ap->channel=(uint8_t)channel;
    if (le16(p+16)&16) { ap->security|=SCAN_PRIVACY; }
    bool have_rsn=false;
    for (size_t at=ie_at;at<ie_at+ie_bytes;) {
        if (ie_at+ie_bytes-at<2) { return BLOCK_RANGE; }unsigned id=p[at],bytes=p[at+1];at+=2;
        if (bytes>ie_at+ie_bytes-at) { return BLOCK_RANGE; }
        if (id==48) {
            if (have_rsn) { return BLOCK_RANGE; }have_rsn=true;ap->security|=SCAN_RSN;
            enum block_result r=rsn(p+at,bytes,ap);if (r!=BLOCK_OK) { return r; }
        } else if (id==221 && bytes>=4 && !p[at] && p[at+1]==80 && p[at+2]==242 && p[at+3]==1) {
            if (bytes<6 || le16(p+at+4)!=1) { return BLOCK_RANGE; }ap->security|=SCAN_WPA;
        }at+=bytes;
    }return BLOCK_OK;
}
void brcm_scan_event(void *context,const void *buffer,size_t n)
{
    struct brcm_scan_state *s=context;if (!s || !buffer || !s->armed || s->complete) { return; }
    const unsigned char *p=buffer;enum block_result bad=BLOCK_RANGE;
    if (n<4 || (p[0]&0xf0)!=0x20) { goto malformed; }
    size_t skip=4u+(size_t)p[3]*4;if (skip>n) { goto malformed; }
    if ((p[2]&15)!=0) { ++s->ignored;return; }p+=skip;n-=skip;
    if (n<72) { goto malformed; }
    if (be16(p+12)!=0x886c || be16(p+14)!=0x8001 || p[19]!=0 || p[20]!=16 || p[21]!=24 || be16(p+22)!=1) { ++s->ignored;return; }
    if (be16(p+24)!=2 || p[70] || p[71]) { ++s->ignored;return; }
    unsigned event=be32(p+28);if (event!=69) { ++s->ignored;return; }
    uint32_t bytes=be32(p+44);if (bytes>n-72) { goto malformed; }
    uint32_t status=be32(p+32);const unsigned char *data=p+72;
    if (bytes && (bytes<12 || le16(data+8)!=s->sync_id)) { ++s->ignored;return; }
    ++s->events;s->event_status=status;s->event_reason=be32(p+36);
    if (status!=8) { s->complete=true;return; }
    if (bytes<12 || le32(data)>bytes || le32(data)<138 || le16(data+10)!=1) { goto malformed; }
    uint32_t size=le32(data)-12;s->last_bss_version=le32(data+12);struct brcm_scan_ap ap;
    bad=brcm_scan_bss(data+12,size,&ap);if (bad!=BLOCK_OK) { goto malformed; }
    for (unsigned i=0;i<s->count;++i) {
        struct brcm_scan_ap *old=&s->ap[i];
        if (old->band==ap.band && equal(old->bssid,ap.bssid,6)) {
            ++s->duplicates;
            if (ap.rssi>old->rssi) { if (!ap.ssid_length && old->ssid_length) { ap.ssid_length=old->ssid_length;for (unsigned j=0;j<old->ssid_length;++j) { ap.ssid[j]=old->ssid[j]; } }*old=ap; }
            else if (!old->ssid_length && ap.ssid_length) { old->ssid_length=ap.ssid_length;for (unsigned j=0;j<ap.ssid_length;++j) { old->ssid[j]=ap.ssid[j]; } }
            return;
        }
    }
    if (s->count==BRCM_SCAN_AP_MAX) { s->truncated=true;++s->overflow;return; }s->ap[s->count++]=ap;return;
malformed:++s->malformed;s->last_parse_error=bad;
}
#define TRY(call) do { r=(call);if (r!=BLOCK_OK) { goto fail; } } while (0)
#define REQUIRE(test,err) do { if (!(test)) { r=(err);goto fail; } } while (0)
enum block_result brcm_scan_run(const struct brcm_scan_io *io,const struct brcm_prepare_state *prep,struct brcm_scan_state *s)
{
    if (!io || !prep || !s || !io->iovar || !io->radio || !io->poll || !io->delay_us || !io->milliseconds) { return BLOCK_ARGUMENT; }
    if (s->attempted) { return s->error; }*s=(struct brcm_scan_state){.phase=1,.sync_id=0x4f53,.error=BLOCK_NOT_READY};
    if (!prep->ready || !prep->clm_complete || !brcm_country_valid(prep->country)) { return s->error; }
    enum block_result r;unsigned char value[80]={0},mask[16];size_t n;
    TRY(io->iovar(io->context,false,"country",NULL,0,value,12,&n,&s->firmware_status));
    REQUIRE(n==12 && value[0]==(unsigned char)prep->country[0] && value[1]==(unsigned char)prep->country[1] && !value[2] && !value[3] && equal(value,value+8,4) && le32(value+4)==prep->country_revision,BLOCK_NOT_READY);
    r=io->iovar(io->context,false,"scan_ver",NULL,0,value,4,&n,&s->firmware_status);
    if (r==BLOCK_OK) { REQUIRE(n==4,BLOCK_RANGE);s->scan_version=le32(value);s->escan_version=2; }
    else if (r==BLOCK_UNSUPPORTED && s->firmware_status==0xffffffe9u) { s->escan_version=1; }
    else { goto fail; }
    s->phase=2;TRY(io->iovar(io->context,false,"event_msgs",NULL,0,s->saved_mask,16,&n,&s->firmware_status));REQUIRE(n==16,BLOCK_RANGE);
    for (unsigned i=0;i<16;++i) { mask[i]=s->saved_mask[i]; }mask[8]|=32;
    s->attempted=true;TRY(io->iovar(io->context,true,"event_msgs",mask,16,NULL,0,&n,&s->firmware_status));
    TRY(io->iovar(io->context,false,"event_msgs",NULL,0,value,16,&n,&s->firmware_status));REQUIRE(n==16 && equal(mask,value,16),BLOCK_IO);
    s->phase=3;s->interface_may_be_up=true;TRY(io->radio(io->context,true,&s->firmware_status));
    s->phase=4;for (unsigned i=0;i<80;++i) { value[i]=0; }
    put32(value,s->escan_version);put16(value+4,1);put16(value+6,s->sync_id);
    unsigned char *params=value+8;unsigned fixed=s->escan_version==2 ? 72 : 64;
    if (s->escan_version==2) { put16(params,2);put16(params+2,72); }
    unsigned ssid_at=s->escan_version==2 ? 4 : 0;
    for (unsigned i=0;i<6;++i) { params[ssid_at+36+i]=255; }params[ssid_at+42]=2;
    unsigned scan_at=s->escan_version==2 ? 48 : 43;
    if (s->escan_version==2) { put32(params+scan_at,1); }else { params[scan_at]=1; }
    unsigned probes=s->escan_version==2 ? 52 : 44;put32(params+probes,UINT32_MAX);put32(params+probes+4,UINT32_MAX);
    put32(params+probes+8,150);put32(params+probes+12,UINT32_MAX); /* passive 150 ms/channel, default legal list */
    s->armed=true;TRY(io->iovar(io->context,true,"escan",value,8+fixed,NULL,0,&n,&s->firmware_status));s->request_accepted=true;
    s->phase=5;uint64_t start=io->milliseconds(io->context);
    for (unsigned i=0;i<3000 && !s->complete;++i) {
        ++s->polls;TRY(io->poll(io->context));if (s->complete) { break; }
        if (io->milliseconds(io->context)-start>=30000) { break; }TRY(io->delay_us(io->context,10000));
    }
    REQUIRE(s->complete,BLOCK_TIMEOUT);REQUIRE(s->event_status==0,BLOCK_IO);
    s->phase=6;TRY(io->radio(io->context,false,&s->firmware_status));s->interface_down=true;s->interface_may_be_up=false;
    s->phase=7;TRY(io->iovar(io->context,true,"event_msgs",s->saved_mask,16,NULL,0,&n,&s->firmware_status));
    TRY(io->iovar(io->context,false,"event_msgs",NULL,0,value,16,&n,&s->firmware_status));REQUIRE(n==16 && equal(value,s->saved_mask,16),BLOCK_IO);s->mask_restored=true;
    s->armed=false;s->phase=8;s->ready=true;s->error=BLOCK_OK;return BLOCK_OK;
fail:s->armed=false;s->error=r;return r;
}
