#include "text_io.h"

static bool print(const char *text)
{
    return text_write(text,uos_length(text));
}

static bool metric(const char *label,uint64_t value)
{
    char digits[20];
    size_t start=sizeof(digits);
    do {
        digits[--start]=(char)('0'+value%10);
        value/=10;
    } while (value);
    return print(label) && text_write(digits+start,sizeof(digits)-start);
}

static bool session(const char *protocol,const struct uos_net_session *s)
{
    if (!print(protocol) || !print(s->online ? " session=up" : " session=down") ||
        !metric(" generation=",s->generation) || !metric(" slots=",s->used) ||
        !metric("/",s->limit)) { return false; }
    if (s->online && (!metric(" address=",s->ipv4[0]) || !metric(".",s->ipv4[1]) ||
        !metric(".",s->ipv4[2]) || !metric(".",s->ipv4[3]) || !metric("/",s->prefix))) { return false; }
    return print("\n");
}

static bool report(const struct uos_net_stats *stats)
{
    const struct uos_tcp_stats *tcp=&stats->tcp;
    const struct uos_udp_stats *udp=&stats->udp;
    return session("TCP",&tcp->session) &&
        metric("  connecting=",tcp->connecting) && metric(" established=",tcp->established) &&
        metric(" failed=",tcp->failed) && metric(" closing=",tcp->closing) && metric(" time_wait=",tcp->time_wait) && print("\n") &&
        metric("  segments_tx=",tcp->segments_tx) && metric(" retransmissions=",tcp->retransmissions) &&
        metric(" bytes_tx=",tcp->bytes_tx) && metric(" bytes_rx=",tcp->bytes_rx) && print("\n") &&
        metric("  fin_acked=",tcp->fin_acked) && metric(" resets=",tcp->resets) && metric(" timeouts=",tcp->timeouts) &&
        metric(" arp_requests=",tcp->arp_requests) && metric(" arp_failures=",tcp->arp_failures) && print("\n") &&
        session("UDP",&udp->session) &&
        metric("  datagrams_tx=",udp->datagrams_tx) && metric(" datagrams_rx=",udp->datagrams_rx) &&
        metric(" bytes_tx=",udp->bytes_tx) && metric(" bytes_rx=",udp->bytes_rx) && print("\n") &&
        metric("  full_drops=",udp->full_drops) && metric(" timeouts=",udp->timeouts) &&
        metric(" arp_requests=",udp->arp_requests) && metric(" arp_failures=",udp->arp_failures) && print("\n");
}

static int check(void)
{
    struct uos_net_stats stats={.version=UINT32_MAX};
    struct uos_socket_info info={.version=UINT32_MAX};
    int64_t permissions=uos_policy_get(0);
    if (permissions<0) { return 1; }
    if (!(permissions&UOS_ALLOW_NETWORK)) {
        if (uos_net_stats(&stats)!=UOS_EACCES || uos_socket_info(UOS_EVENT_TCP,0,&info)!=UOS_EACCES ||
            stats.version!=UINT32_MAX || info.version!=UINT32_MAX) { return 1; }
        return print("PASS: network diagnostics honor process permissions.\n") ? 0 : 1;
    }
    static const struct uos_net_stats read_only={0};
    if (uos_call2(UOS_SYS_NET_STATS,(uintptr_t)&stats,sizeof(stats)-1)!=UOS_ERROR ||
        uos_call2(UOS_SYS_NET_STATS,(uintptr_t)&read_only,sizeof(read_only))!=UOS_EFAULT ||
        stats.version!=UINT32_MAX || uos_socket_info(UOS_EVENT_TIMER,0,&info)!=UOS_ERROR ||
        uos_socket_info(UOS_EVENT_TCP,0,NULL)!=UOS_EFAULT || uos_socket_info(UOS_EVENT_UDP,0,&info)!=UOS_EBADF ||
        info.version!=UINT32_MAX || uos_net_stats(&stats) || stats.version!=UOS_NET_STATS_VERSION || stats.reserved ||
        stats.tcp.session.reserved || stats.udp.session.reserved || stats.tcp.reserved ||
        stats.tcp.session.used!=stats.tcp.connecting+stats.tcp.established+stats.tcp.failed+stats.tcp.closing+stats.tcp.time_wait ||
        stats.tcp.session.used>stats.tcp.session.limit || stats.udp.session.used>stats.udp.session.limit) { return 1; }
    return print("PASS: network snapshot ABI, checked output and socket lookup.\n") ? 0 : 1;
}

int app_main(void)
{
    int64_t argc=uos_argc();
    if (argc==2) {
        char arg[8];
        if (uos_arg(1,arg,sizeof(arg))==5 && arg[0]=='c' && arg[1]=='h' && arg[2]=='e' &&
            arg[3]=='c' && arg[4]=='k') { return check(); }
    }
    if (argc!=1) { print("Usage: netstat [check]\n");return 2; }
    struct uos_net_stats stats;
    int64_t result=uos_net_stats(&stats);
    if (result==UOS_EACCES) { print("netstat requires network permission.\n");return 1; }
    if (result || stats.version!=UOS_NET_STATS_VERSION) { print("Network statistics unavailable.\n");return 1; }
    return report(&stats) ? 0 : 1;
}
