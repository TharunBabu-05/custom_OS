/* Original foreground networking console; no libc or background task. */
#include "network_session.h"
#include "crypto.h"
static bool same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static void write(const struct network_session_io *io,const char *s) { io->write(io->link.context,s); }
static void number(const struct network_session_io *io,uint64_t n)
{ char p[21];unsigned at=20;p[at]=0;do { p[--at]=(char)('0'+n%10);n/=10; }while (n);write(io,p+at); }
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
    write(io,"net> ");
}
enum block_result network_session_run(const struct network_session_io *io,struct netprobe *n,struct network_session *s)
{
    if (!io || !n || !s || !io->getc || !io->write || !io->random || !io->inspect || !io->info || !io->link.milliseconds || !io->link.delay_us || !n->live || !n->active) { return BLOCK_ARGUMENT; }
    if (s->attempted) { return BLOCK_NOT_READY; }*s=(struct network_session){.attempted=true,.active=true,.started=io->link.milliseconds(io->link.context),.result=BLOCK_NOT_READY};
    enum block_result r=io->inspect(io->link.context);uint64_t last_check=s->started;unsigned seen_pings=n->ping_finished;char line[128];unsigned length=0;bool overflow=false,stop=false;
    if (r!=BLOCK_OK) { s->reason=NS_LINK; }
    else { write(io,"\nWi-Fi session OPEN. Commands: ping [numeric IPv4], netinfo, help, disconnect.\nLease expiry, unsupported rekey/link failure or session limit closes it. Ctrl+C disconnects.\n");io->info(io->link.context);write(io,"net> "); }
    for (unsigned i=0;r==BLOCK_OK && !stop && i<360000;++i) {
        ++s->polls;r=netprobe_service(&io->link,n);uint64_t now=io->link.milliseconds(io->link.context);
        if (r!=BLOCK_OK) {
            s->reason=n->conflict ? NS_CONFLICT : r==BLOCK_TIMEOUT ? now-n->lease_origin>=(uint64_t)n->lease.seconds*1000 ? NS_LEASE : NS_LIMIT : NS_LINK;break;
        }
        if (now-last_check>=15000) { r=io->inspect(io->link.context);last_check=now;if (r!=BLOCK_OK) { s->reason=NS_LINK;break; } }
        if (n->ping_finished!=seen_pings) { seen_pings=n->ping_finished;if (n->ping_result==BLOCK_OK) { ++s->pings_ok; }else { ++s->pings_timeout; }ping_result(io,n); }
        /* Drain at most one small UART FIFO per iteration, then service link
         * again. A long input line cannot starve networking or expiry checks. */
        for (unsigned k=0;k<16;++k) {
            int c=io->getc(io->link.context);if (c<0) { break; }
            if (c==3) { s->reason=NS_USER;stop=true;break; }
            if (c=='\r' || c=='\n') {
                write(io,"\n");line[length]=0;++s->commands;
                if (overflow) { write(io,"Input too long; command discarded.\n"); }
                else if (same(line,"disconnect") || same(line,"exit")) { s->reason=NS_USER;stop=true;break; }
                else if (same(line,"help")) { write(io,"ping [numeric IPv4] (default gateway; off-link targets use gateway)\nnetinfo disconnect; DNS and other on-link neighbors are not implemented.\n"); }
                else if (same(line,"netinfo")) { r=io->inspect(io->link.context);if (r!=BLOCK_OK) { s->reason=NS_LINK;stop=true;break; }io->info(io->link.context); }
                else if (same(line,"ping") || (line[0]=='p' && line[1]=='i' && line[2]=='n' && line[3]=='g' && line[4]==' ')) {
                    unsigned char target[4],entropy[32]={0};bool valid=true;
                    if (same(line,"ping")) { for (unsigned j=0;j<4;++j) { target[j]=n->lease.router[j]; } }else { valid=network_parse_ipv4(line+5,target); }
                    if (!valid) { write(io,"Use numeric IPv4, for example ping 1.1.1.1. DNS is not implemented.\n"); }
                    else if (n->phase!=NP_READY) { write(io,"Ping already in progress; disconnect remains available.\n"); }
                    else if (!io->random(io->link.context,entropy)) { r=BLOCK_IO;s->reason=NS_IO;stop=true; }
                    else {
                        enum block_result p=netprobe_ping_start(n,target,entropy,now);
                        if (p==BLOCK_OK) { ++s->pings_started;write(io,"Sending three ICMP requests. Network console stays responsive.\n"); }
                        else { write(io,"Ping refused: invalid route, expired lease or unusable fresh nonce. Gateway and off-link unicast IPv4 are supported.\n"); }
                    }
                    crypto_wipe(entropy,sizeof(entropy));
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
