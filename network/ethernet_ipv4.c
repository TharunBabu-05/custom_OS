#include "ethernet_ipv4.h"
#include "net_wire.h"
#include <string.h>

enum { ETHERNET_HEADER=14, ARP_HEADER=28, IPV4_HEADER=20, ICMP_HEADER=8 };
static const uint8_t broadcast[]={255,255,255,255,255,255};
static const uint8_t zero_mac[6];

static bool usable(uint32_t ip,uint32_t mask)
{
    uint32_t host=ip&~mask;
    unsigned first=ip>>24;
    return first && first!=127 && first<224 && host && host!=~mask;
}

bool ethernet_ipv4_configure(struct ethernet_ipv4 *config,const uint8_t address[4],unsigned prefix)
{
    if (!prefix || prefix>30) { return false; }
    uint32_t mask=UINT32_MAX<<(32-prefix);
    if (!usable(net_be32(address),mask)) { return false; }
    memcpy(config->address,address,4);
    net_put32(config->mask,mask);
    config->prefix=(uint8_t)prefix;
    return true;
}

static bool same_subnet(const struct ethernet_ipv4 *config,const uint8_t *source)
{
    uint32_t mask=net_be32(config->mask),ip=net_be32(source),local=net_be32(config->address);
    return usable(ip,mask) && ip!=local && !((ip^local)&mask);
}

size_t ethernet_ipv4_reply(const struct ethernet_ipv4 *config,const uint8_t mac[6],
                           const uint8_t *frame,size_t bytes,uint8_t *out,size_t capacity)
{
    if (bytes<ETHERNET_HEADER+ARP_HEADER || bytes>1514 || capacity<60 ||
        (frame[6]&1) || net_equal(frame+6,zero_mac,6) || net_equal(frame+6,mac,6)) { return 0; }
    const uint8_t *request=frame+ETHERNET_HEADER;
    bool unicast=net_equal(frame,mac,6);
    unsigned type=net_be16(frame+12);
    size_t length=ETHERNET_HEADER+ARP_HEADER;
    unsigned ip_length=0;
    if (type==0x806) {
        if ((!unicast && !net_equal(frame,broadcast,6)) || net_be32(request)!=0x00010800 ||
            net_be32(request+4)!=0x06040001 || !net_equal(request+8,frame+6,6) ||
            !net_equal(request+24,config->address,4) ||
            (net_be32(request+14) && !same_subnet(config,request+14))) { return 0; }
    } else if (type==0x800) {
        ip_length=net_be16(request+2);
        const uint8_t *icmp=request+IPV4_HEADER;
        length=ETHERNET_HEADER+ip_length;
        if (!unicast || request[0]!=0x45 || !request[8] || request[9]!=1 ||
            ip_length<IPV4_HEADER+ICMP_HEADER || length>bytes || length>capacity ||
            (net_be16(request+6)&0xbfff) || !net_equal(request+16,config->address,4) ||
            !same_subnet(config,request+12) || net_be16(icmp)!=0x0800 ||
            net_checksum(request,IPV4_HEADER,0) || net_checksum(icmp,ip_length-IPV4_HEADER,0)) { return 0; }
    } else {
        return 0;
    }

    memcpy(out,frame,length);
    memcpy(out,frame+6,6);
    memcpy(out+6,mac,6);
    if (length<60) { memset(out+length,0,60-length); }
    uint8_t *reply=out+ETHERNET_HEADER;
    if (type==0x806) {
        reply[7]=2;
        memcpy(reply+8,mac,6);
        memcpy(reply+14,config->address,4);
        memcpy(reply+18,frame+6,6);
        memcpy(reply+24,request+14,4);
    } else {
        uint8_t *icmp=reply+IPV4_HEADER;
        reply[1]=0;
        net_put16(reply+4,0);
        net_put16(reply+6,0x4000);
        reply[8]=64;
        memcpy(reply+12,config->address,4);
        memcpy(reply+16,request+12,4);
        net_put16(reply+10,0);
        net_put16(reply+10,net_checksum(reply,IPV4_HEADER,0));
        memset(icmp,0,4);
        net_put16(icmp+2,net_checksum(icmp,ip_length-IPV4_HEADER,0));
    }
    return length<60 ? 60 : length;
}
