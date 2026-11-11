/* Original bounded external-SAE frame codec. Definitions: brcmfmac
 * cfg80211.c (mgmt_tx/auth RX), fwil_types.h and fweh.h. No radio I/O here. */
#include "brcm_auth.h"
static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0]|(unsigned)p[1]<<8); }
static uint16_t be16(const unsigned char *p) { return (uint16_t)((unsigned)p[0]<<8|p[1]); }
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static void put16(unsigned char *p,unsigned v) { p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8); }
static void put32(unsigned char *p,uint32_t v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(8*i)); } }
static bool equal(const unsigned char *a,const unsigned char *b,size_t n)
{ for (size_t i=0;i<n;++i) { if (a[i]!=b[i]) { return false; } }return true; }
static bool mac(const unsigned char *p)
{ if (p[0]&1) { return false; }unsigned any=0;for (unsigned i=0;i<6;++i) { any|=p[i]; }return !!any; }
static bool peer_valid(const struct brcm_auth_peer *p)
{ return p && mac(p->local) && mac(p->peer) && !equal(p->local,p->peer,6) && p->ssid_length && p->ssid_length<=32 && ((p->band==2 && p->channel>=1 && p->channel<=14) || (p->band==5 && p->channel>=32 && p->channel<=196)); }
enum block_result brcm_auth_encode(const struct brcm_auth_peer *peer,uint32_t id,unsigned sequence,const void *body,size_t bytes,void *output,size_t capacity,size_t *actual)
{
    if (actual) { *actual=0; }
    if (!peer_valid(peer) || !id || !body || !output || !actual) { return BLOCK_ARGUMENT; }
    if ((sequence!=1 && sequence!=2) || bytes!=(sequence==1 ? 98u : 34u)) { return BLOCK_UNSUPPORTED; }
    const unsigned char *b=body;if (sequence==1 && (b[0]!=19 || b[1])) { return BLOCK_UNSUPPORTED; }
    if (sequence==2 && (!le16(b) || le16(b)==65535)) { return BLOCK_RANGE; }
    size_t total=38+bytes;if (capacity<total) { return BLOCK_RANGE; }
    /* Stage first so caller buffers may overlap. Offset 26..27 is ABI padding. */
    unsigned char payload[BRCM_AUTH_PARAMS_MAX]={0};put32(payload+4,4000);put16(payload+8,(unsigned)bytes+6);
    put16(payload+10,0xb0);put16(payload+12,peer->channel);
    for (unsigned i=0;i<6;++i) { payload[14+i]=peer->peer[i];payload[20+i]=peer->peer[i]; }
    put32(payload+28,id);put16(payload+32,3);put16(payload+34,sequence);put16(payload+36,sequence==1 && !peer->hunting ? 126 : 0);
    for (size_t i=0;i<bytes;++i) { payload[38+i]=b[i]; }for (size_t i=0;i<total;++i) { ((unsigned char *)output)[i]=payload[i]; }
    *actual=total;return BLOCK_OK;
}
static bool channel_matches(const struct brcm_auth_peer *peer,unsigned chanspec)
{
    unsigned band=chanspec&0xc000,bw=chanspec&0x3800,sb=(chanspec>>8)&7;int channel=chanspec&255;
    if ((peer->band==2 ? 0u : 0xc000u)!=band) { return false; }
    if (bw==0x1000 && !sb) { }
    else if (bw==0x1800 && sb<2) { channel+=(int)sb*4-2; }
    else if (bw==0x2000 && sb<4) { channel+=(int)sb*4-6; }
    else if (bw==0x2800 && sb<8) { channel+=(int)sb*4-14; }
    else { return false; }return channel==peer->channel;
}
enum block_result brcm_auth_event(const struct brcm_auth_peer *peer,uint32_t id,const void *buffer,size_t n,struct brcm_auth_message *out)
{
    if (!out) { return BLOCK_ARGUMENT; }*out=(struct brcm_auth_message){0};
    if (!peer_valid(peer) || !buffer) { return BLOCK_ARGUMENT; }const unsigned char *p=buffer;
    if (n<4 || (p[0]&0xf0)!=0x20) { return BLOCK_RANGE; }
    size_t skip=4u+(size_t)p[3]*4;if (skip>n) { return BLOCK_RANGE; }
    if ((p[2]&15)!=0) { return BLOCK_NOT_READY; }p+=skip;n-=skip;
    if (n<72) { return BLOCK_RANGE; }
    if (be16(p+12)!=0x886c || be16(p+14)!=0x8001 || p[18] || p[19] || p[20]!=16 || p[21]!=24 || be16(p+22)!=1 || be16(p+24)!=2) { return BLOCK_UNSUPPORTED; }
    uint32_t event=be32(p+28),bytes=be32(p+44);
    out->envelope_valid=true;out->wire_event=event;out->wire_status=be32(p+32);out->wire_bytes=bytes;out->wire_interface=p[70];out->wire_bss=p[71];
    out->wire_flags=be16(p+26);out->wire_reason=be32(p+36);for (unsigned i=0;i<6;++i) { out->wire_peer[i]=p[48+i]; }
    if (p[70] || p[71]) { out->filter=AUTH_FILTER_INTERFACE;return BLOCK_NOT_READY; }
    if (bytes>n-72) { return BLOCK_RANGE; }
    if (event<187 || event>190) { return BLOCK_NOT_READY; }
    const unsigned char *data=p+72;struct brcm_auth_message m=*out;m.kind=(enum brcm_auth_kind)event;m.status=be32(p+32);m.reason=be32(p+36);
    if (event>=189) {
        if (bytes!=4) { return BLOCK_RANGE; }m.packet_id=le32(data);m.wire_cookie=m.packet_id;out->wire_cookie=m.packet_id;
        if (!id || m.packet_id!=id) { out->filter=AUTH_FILTER_COOKIE;return BLOCK_NOT_READY; }
    } else {
        if (event==187) {
            if (bytes!=44 && bytes!=60) { return BLOCK_RANGE; }
            uint32_t ssid_bytes=le32(data+8);
            if (!equal(data+2,peer->peer,6)) { out->filter=AUTH_FILTER_PEER;return BLOCK_NOT_READY; }
            if (ssid_bytes!=peer->ssid_length || !equal(data+12,peer->ssid,peer->ssid_length)) { out->filter=AUTH_FILTER_SSID;return BLOCK_NOT_READY; }
            m.request_flags=le16(data);
        } else {
            if (!equal(p+48,peer->peer,6)) { out->filter=AUTH_FILTER_PEER;return BLOCK_NOT_READY; }
            if (m.status) { *out=m;return BLOCK_IO; }
            if (bytes<16) { *out=m;return BLOCK_RANGE; }
            m.rx_metadata_version=be16(data);
            /* Vendor wl_event_rx_frame_data_v1 and brcmf_rx_mgmt_data are
             * both 16 bytes: BE version/chanspec/RSSI/mactime/rate. Version
             * 1 is observed on the Pi. Keep legacy v0 fixtures; v2 has a
             * different header and must never use these offsets. */
            if (m.rx_metadata_version>1) { *out=m;return BLOCK_UNSUPPORTED; }
            if (bytes<46) { *out=m;return BLOCK_RANGE; }
            if (!channel_matches(peer,be16(data+2))) { out->filter=AUTH_FILTER_CHANNEL;return BLOCK_NOT_READY; }
            /* Like the firmware reference, skip the supplied 24-byte header
             * and bind RX to the event peer/interface rather than its MACs. */
            const unsigned char *auth=data+40;m.sequence=le16(auth+2);m.auth_status=le16(auth+4);
            m.wire_body_bytes=bytes-46;if (m.wire_body_bytes>=2) { m.wire_group=le16(auth+6); }
            if (le16(auth)!=3 || (m.sequence!=1 && m.sequence!=2)) { *out=m;return BLOCK_UNSUPPORTED; }
            if (m.auth_status!=(m.sequence==1 && !peer->hunting ? 126 : 0)) { *out=m;return BLOCK_UNSUPPORTED; }
            size_t required=m.sequence==1 ? 98 : 34;if (bytes!=46+required) { *out=m;return BLOCK_RANGE; }m.body_bytes=required;
            for (size_t i=0;i<m.body_bytes;++i) { m.body[i]=auth[6+i]; }
        }
    }
    *out=m;return event==187 && m.status ? BLOCK_IO : BLOCK_OK;
}
const char *brcm_auth_error(enum block_result result)
{
    static const char *const errors[]={"ok","invalid authentication argument","malformed/out-of-range authentication data","authentication timeout",
        "firmware/transport I/O error","unsupported authentication format/state","authentication path not ready"};
    return (unsigned)result<sizeof(errors)/sizeof(errors[0]) ? errors[result] : "unknown authentication error";
}
