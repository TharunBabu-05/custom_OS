/* Original foreground networking console; no libc or background task. */
#include "network_session.h"
#include "crypto.h"
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static void write(const struct network_session_io *io,const char *s) { io->write(io->link.context,s); }
static void number(const struct network_session_io *io,uint64_t n)
{ char p[21];unsigned at=20;p[at]=0;do { p[--at]=(char)('0'+n%10);n/=10; }while (n);write(io,p+at); }
static void address(const struct network_session_io *io,const unsigned char p[4])
{ for (unsigned i=0;i<4;++i) { if (i) { write(io,"."); }number(io,p[i]); } }
static bool prefix(const char *p,const char *start) { while (*start) { if (*p++!=*start++) { return false; } }return true; }
static bool numeric(const char *p) { if (!*p) { return false; }for (;*p;++p) { if ((*p<'0' || *p>'9') && *p!='.') { return false; } }return true; }
bool network_parse_ipv4(const char *p,unsigned char address[4])
{
    if (!p || !address) { return false; }unsigned char out[4];
    for (unsigned i=0;i<4;++i) {
        unsigned value=0,digits=0;while (*p>='0' && *p<='9') { if (++digits>3) { return false; }value=value*10+(unsigned)(*p++-'0'); }
        if (!digits || value>255 || (i==3 ? *p!=0 : *p++!='.')) { return false; }out[i]=(unsigned char)value;
    }
    if (!out[0] || out[0]==127 || out[0]>=224) { return false; }for (unsigned i=0;i<4;++i) { address[i]=out[i]; }return true;
}
static void ping_result(const struct network_session_io *io,const struct netprobe *n)
{
    write(io,"\nPing complete: sent=");number(io,n->echoes);write(io," reply_mask=");number(io,n->received_mask);write(io,n->ping_result==BLOCK_OK ? " (reply verified)\n" : " (no complete successful ping set; Wi-Fi stays open)\n");
    for (unsigned j=0;j<3;++j) { if (n->received_mask&(1u<<j)) { write(io,"sequence=");number(io,j+1);write(io," observed_latency_ms=");number(io,n->latency_ms[j]);write(io,"\n"); } }
}
static void redraw(const struct network_session_io *io,char *line,unsigned length)
{ line[length]=0;write(io,"net> ");write(io,line); }
static enum block_result ping_begin(const struct network_session_io *io,struct netprobe *n,struct network_session *s,const unsigned char target[4],uint64_t now)
{
    unsigned char entropy[32]={0};bool random=io->random(io->link.context,entropy);enum block_result r=BLOCK_IO;
    if (random) {
        r=netprobe_ping_start(n,target,entropy,now);
        if (r==BLOCK_OK) { ++s->pings_started;write(io,"Sending three ICMP requests to ");address(io,target);write(io,". Network console stays responsive.\n"); }
        else { write(io,"Ping refused: invalid route, expired lease or unusable fresh nonce. Gateway and off-link unicast IPv4 are supported.\n"); }
    }
    crypto_wipe(entropy,sizeof(entropy));return random ? BLOCK_OK : BLOCK_IO;
}
enum block_result network_session_run(const struct network_session_io *io,struct netprobe *n,struct network_session *s)
{
    if (!io || !n || !s || !io->getc || !io->write || !io->random || !io->inspect || !io->info || !io->link.milliseconds || !io->link.delay_us || !n->live || !n->active) { return BLOCK_ARGUMENT; }
    if (s->attempted) { return BLOCK_NOT_READY; }*s=(struct network_session){.attempted=true,.active=true,.started=io->link.milliseconds(io->link.context),.result=BLOCK_NOT_READY};
    enum block_result r=io->inspect(io->link.context);uint64_t last_check=s->started;unsigned seen_pings=n->ping_finished,seen_dns=n->dns_finished;char line[272];unsigned length=0;bool overflow=false,stop=false,ping_after_dns=false;
    if (r!=BLOCK_OK) { s->reason=NS_LINK; }
    else { write(io,"\nWi-Fi session OPEN. Commands: ping [IPv4 or hostname], nslookup <hostname>, netinfo, help, disconnect.\nLease expiry, unsupported rekey/link failure or session limit closes it. Ctrl+C disconnects.\n");io->info(io->link.context);write(io,"net> "); }
    for (unsigned i=0;r==BLOCK_OK && !stop && i<360000;++i) {
        ++s->polls;r=netprobe_service(&io->link,n);uint64_t now=io->link.milliseconds(io->link.context);
        if (r!=BLOCK_OK) {
            s->reason=n->conflict ? NS_CONFLICT : r==BLOCK_TIMEOUT ? now-n->lease_origin>=(uint64_t)n->lease.seconds*1000 ? NS_LEASE : NS_LIMIT : NS_LINK;break;
        }
        if (now-last_check>=15000) { r=io->inspect(io->link.context);last_check=now;if (r!=BLOCK_OK) { s->reason=NS_LINK;break; } }
        if (n->ping_finished!=seen_pings) { seen_pings=n->ping_finished;if (n->ping_result==BLOCK_OK) { ++s->pings_ok; }else { ++s->pings_timeout; }ping_result(io,n);redraw(io,line,length); }
        if (n->dns_finished!=seen_dns) {
            seen_dns=n->dns_finished;write(io,"\nDNS ");write(io,n->dns.name);write(io,": ");write(io,dns_status_text(n->dns.status));
            if (n->dns.status==DNS_SUCCESS) { ++s->dns_ok;write(io," -> ");address(io,n->dns.address);write(io," TTL=");number(io,n->dns.ttl);write(io," CNAME_hops=");number(io,n->dns.cnames); }
            else { ++s->dns_failed; }write(io,"\n");
            if (ping_after_dns && n->dns.status==DNS_SUCCESS) { r=ping_begin(io,n,s,n->dns.address,now);if (r!=BLOCK_OK) { s->reason=NS_IO;break; } }
            ping_after_dns=false;redraw(io,line,length);
        }
        /* Drain at most one small UART FIFO per iteration, then service link
         * again. A long input line cannot starve networking or expiry checks. */
        for (unsigned k=0;k<16;++k) {
            int c=io->getc(io->link.context);if (c<0) { break; }
            if (c==3) { s->reason=NS_USER;stop=true;break; }
            if (c=='\r' || c=='\n') {
                write(io,"\n");line[length]=0;++s->commands;
                if (overflow) { write(io,"Input too long; command discarded.\n"); }
                else if (same(line,"disconnect") || same(line,"exit")) { s->reason=NS_USER;stop=true;break; }
                else if (same(line,"help")) { write(io,"ping [IPv4 or hostname] (default gateway; off-link targets use gateway)\nnslookup <hostname> (DHCP DNS, A/IN, UDP; no cache or TCP fallback)\nnetinfo disconnect; other on-link neighbors are not implemented.\n"); }
                else if (same(line,"netinfo")) { r=io->inspect(io->link.context);if (r!=BLOCK_OK) { s->reason=NS_LINK;stop=true;break; }io->info(io->link.context); }
                else if (same(line,"ping") || prefix(line,"ping ") || prefix(line,"nslookup ")) {
                    bool ping=!prefix(line,"nslookup "),default_ping=same(line,"ping");const char *name=default_ping ? NULL : line+(ping ? 5 : 9);
                    unsigned char target[4];char hostname[DNS_NAME_MAX+1];bool literal=default_ping || (ping && network_parse_ipv4(name,target));
                    if (default_ping) { for (unsigned j=0;j<4;++j) { target[j]=n->lease.router[j]; } }
                    if (!literal && (numeric(name) || !dns_name_normalize(name,hostname))) { write(io,"Use a valid IPv4 address or hostname, for example ping www.google.com.\n"); }
                    else if (n->phase!=NP_READY) { write(io,"Network operation already in progress; netinfo and disconnect remain available.\n"); }
                    else if (literal) { r=ping_begin(io,n,s,target,now);if (r!=BLOCK_OK) { s->reason=NS_IO;stop=true; } }
                    else {
                        unsigned char entropy[32]={0};
                        if (!io->random(io->link.context,entropy)) { r=BLOCK_IO;s->reason=NS_IO;stop=true; }
                        else if (netprobe_dns_start(n,hostname,entropy,now)==BLOCK_OK) { ++s->dns_started;ping_after_dns=ping;write(io,"Resolving ");write(io,hostname);write(io," using DHCP DNS ");address(io,n->lease.dns);write(io,". Console stays responsive.\n"); }
                        else { write(io,"DNS refused: no supported DHCP DNS route, expired lease or unusable fresh transaction. Wi-Fi stays open.\n"); }
                        crypto_wipe(entropy,sizeof(entropy));
                    }
                }else if (length) { write(io,"Unknown network command. Type help.\n"); }
                length=0;overflow=false;write(io,"net> ");
            }else if (c=='\b' || c==127) { if (!overflow && length) { --length;write(io,"\b \b"); } }
            else if (c==21) { length=0;overflow=false;write(io,"\nnet> "); }
            else if (c>=32 && c<=126 && !overflow) { if (length+1<sizeof(line)) { line[length++]=(char)c;char out[2]={(char)c,0};write(io,out); }else { overflow=true; } }
        }
        if (r!=BLOCK_OK || stop) { break; }r=io->link.delay_us(io->link.context,10000);if (r!=BLOCK_OK) { s->reason=NS_IO; }
    }
    if (!s->reason) { s->reason=NS_LIMIT;r=BLOCK_TIMEOUT; }
    s->active=false;s->ended=true;s->ended_at=io->link.milliseconds(io->link.context);s->result=r;netprobe_close(n);crypto_wipe(line,sizeof(line));
    write(io,"\nWi-Fi session closing; deauthorization/DOWN/key cleanup follows. Reason=");number(io,s->reason);write(io,"\n");return r;
}
