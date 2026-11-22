#include "tls13_client.h"
#include "tls13_keys.h"
#include "aes_gcm.h"
#include "p256.h"
#include "crypto.h"
#include <stdint.h>

enum { CCS=20,ALERT=21,HANDSHAKE=22,APPLICATION=23,TAG_BYTES=16 };
enum { RECORD_HEADER=5,RECORD_MAX=TLS13_TRANSCRIPT_MAX+256,RECORD_COUNT_MAX=64 };
struct record { unsigned char data[RECORD_HEADER+RECORD_MAX];size_t size;unsigned type; };
struct traffic { unsigned char key[16],iv[12];uint64_t sequence; };

static void copy_bytes(unsigned char *to,const unsigned char *from,size_t n)
{ for (size_t i=0;i<n;++i) { to[i]=from[i]; } }

static unsigned u16(const unsigned char *p)
{ return ((unsigned)p[0]<<8)|p[1]; }

static void put16(unsigned char *p,size_t n)
{ p[0]=(unsigned char)(n>>8);p[1]=(unsigned char)n; }

static size_t message_bytes(const unsigned char *p)
{ return 4+((size_t)p[1]<<16)+((size_t)p[2]<<8)+p[3]; }

static size_t hostname_bytes(const char *name)
{
    if (!name) { return 0; }
    size_t n=0,label=0;
    for (;n<254 && name[n];++n) {
        unsigned char c=(unsigned char)name[n];
        if (c=='.') {
            if (!label || name[n-1]=='-') { return 0; }
            label=0;
        } else {
            if (!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ||
                (c=='-' && label)) || ++label>63) { return 0; }
        }
    }
    return n<=253 && label && name[n-1]!='-' ? n : 0;
}

static size_t client_hello(unsigned char *out,const unsigned char random[64],
    const unsigned char share[64],const char *name,size_t name_bytes)
{
    static const unsigned char suites[]={0,2,0x13,1,1,0};
    static const unsigned char extensions[]={
        0,43,0,3,2,3,4,             /* supported_versions */
        0,10,0,4,0,2,0,23,          /* supported_groups */
        0,13,0,4,0,2,4,3,           /* signature_algorithms */
        0,51,0,71,0,69,0,23,0,65,4  /* key_share */
    };
    size_t at=71+sizeof(suites);
    size_t bytes=at+2+sizeof(extensions)+64+9+name_bytes;
    out[0]=1;out[1]=0;put16(out+2,bytes-4);
    out[4]=3;out[5]=3;
    copy_bytes(out+6,random,32);
    out[38]=32;
    copy_bytes(out+39,random+32,32);
    copy_bytes(out+71,suites,sizeof(suites));
    put16(out+at,bytes-at-2);
    at+=2;
    copy_bytes(out+at,extensions,sizeof(extensions));at+=sizeof(extensions);
    copy_bytes(out+at,share,64);at+=64;
    put16(out+at,0);put16(out+at+2,name_bytes+5);put16(out+at+4,name_bytes+3);
    out[at+6]=0;put16(out+at+7,name_bytes);
    copy_bytes(out+at+9,(const unsigned char *)name,name_bytes);
    return at+9+name_bytes;
}

static enum tls13_client_result receive(const struct tls13_io *io,struct record *record)
{
    if (!io->read_exact(io->context,record->data,RECORD_HEADER)) { return TLS13_CLIENT_IO; }
    record->type=record->data[0];
    record->size=u16(record->data+3);
    if (u16(record->data+1)!=0x0303 || !record->size || record->size>RECORD_MAX) { return TLS13_CLIENT_RECORD; }
    return io->read_exact(io->context,record->data+RECORD_HEADER,record->size) ? TLS13_CLIENT_OK : TLS13_CLIENT_IO;
}

static void record_header(struct record *record,unsigned type,unsigned version,size_t bytes)
{
    record->data[0]=(unsigned char)type;
    put16(record->data+1,version);
    put16(record->data+3,bytes);
}

static bool traffic_keys(struct traffic *out,const unsigned char secret[32])
{
    out->sequence=0;
    return tls13_expand(secret,"key",NULL,0,out->key,sizeof(out->key)) &&
        tls13_expand(secret,"iv",NULL,0,out->iv,sizeof(out->iv));
}

static void record_nonce(struct traffic *keys,unsigned char nonce[12])
{
    copy_bytes(nonce,keys->iv,sizeof(keys->iv));
    for (unsigned i=0;i<8;++i) { nonce[11-i]^=(unsigned char)(keys->sequence>>(8*i)); }
    ++keys->sequence;
}

static bool unprotect(struct record *record,struct traffic *keys)
{
    if (record->type!=APPLICATION || record->size<=TAG_BYTES) { return false; }
    unsigned char nonce[12],*payload=record->data+RECORD_HEADER;
    size_t bytes=record->size-TAG_BYTES;
    record_nonce(keys,nonce);
    if (!aes128_gcm_open(keys->key,nonce,record->data,RECORD_HEADER,payload,bytes,payload+bytes)) { return false; }
    while (bytes && !payload[bytes-1]) { --bytes; }
    if (!bytes) { return false; }
    record->type=payload[bytes-1];
    record->size=bytes-1;
    return true;
}

static bool send_protected(const struct tls13_io *io,struct record *record,struct traffic *keys,
    unsigned type,const void *data,size_t bytes)
{
    unsigned char nonce[12],*payload=record->data+RECORD_HEADER;
    size_t encrypted=bytes+1+TAG_BYTES;
    record_header(record,APPLICATION,0x0303,encrypted);
    copy_bytes(payload,data,bytes);
    payload[bytes]=(unsigned char)type;
    record_nonce(keys,nonce);
    return aes128_gcm_seal(keys->key,nonce,record->data,RECORD_HEADER,payload,bytes+1,payload+bytes+1) &&
        io->write_all(io->context,record->data,RECORD_HEADER+encrypted);
}

static enum tls13_client_result read_hello(const struct tls13_io *io,struct record *record,
    unsigned char *transcript,size_t *length)
{
    size_t start=*length;
    while (*length-start<4 || *length-start<message_bytes(transcript+start)) {
        enum tls13_client_result r=receive(io,record);
        if (r!=TLS13_CLIENT_OK) { return r; }
        if (record->type!=HANDSHAKE || record->size>TLS13_TRANSCRIPT_MAX-*length) { return TLS13_CLIENT_HELLO; }
        copy_bytes(transcript+*length,record->data+RECORD_HEADER,record->size);
        *length+=record->size;
    }
    return *length-start==message_bytes(transcript+start) ? TLS13_CLIENT_OK : TLS13_CLIENT_HELLO;
}

static bool server_extensions(const unsigned char *p,size_t bytes)
{
    if (bytes<2 || u16(p)!=bytes-2) { return false; }
    p+=2;bytes-=2;
    bool sni=false,groups=false;
    while (bytes) {
        if (bytes<4) { return false; }
        unsigned type=u16(p),n=u16(p+2);
        p+=4;bytes-=4;
        if (n>bytes) { return false; }
        if (type==0 && !sni && !n) {
            sni=true;
        } else if (type==10 && !groups && n>=4 && !(n&1) && u16(p)==n-2) {
            groups=true;
        } else {
            return false;
        }
        p+=n;bytes-=n;
    }
    return true;
}

static enum tls13_client_result read_server_flight(const struct tls13_io *io,struct record *record,
    struct traffic *keys,unsigned char *transcript,size_t *length)
{
    static const unsigned char order[]={8,11,15,20};
    size_t cursor=*length,index=0;
    bool ccs=false;
    for (unsigned records=0;records<RECORD_COUNT_MAX;++records) {
        enum tls13_client_result r=receive(io,record);
        if (r!=TLS13_CLIENT_OK) { return r; }
        if (record->type==CCS && !ccs && !keys->sequence &&
            record->size==1 && record->data[RECORD_HEADER]==1) { ccs=true;continue; }
        if (!unprotect(record,keys) || record->type!=HANDSHAKE || !record->size ||
            record->size>TLS13_TRANSCRIPT_MAX-*length) { return TLS13_CLIENT_RECORD; }
        copy_bytes(transcript+*length,record->data+RECORD_HEADER,record->size);
        *length+=record->size;
        while (*length-cursor>=4) {
            size_t message=message_bytes(transcript+cursor);
            if (transcript[cursor]!=order[index] || message>TLS13_TRANSCRIPT_MAX-cursor) { return TLS13_CLIENT_AUTH; }
            if (message>*length-cursor) { break; }
            if (!index && !server_extensions(transcript+cursor+4,message-4)) { return TLS13_CLIENT_HELLO; }
            cursor+=message;
            if (++index==sizeof(order)) { return cursor==*length ? TLS13_CLIENT_OK : TLS13_CLIENT_AUTH; }
        }
    }
    return TLS13_CLIENT_RECORD;
}

static enum tls13_client_result read_response(const struct tls13_io *io,struct record *record,struct traffic *keys,
    unsigned char *pending,tls13_response_fn response,void *context)
{
    size_t buffered=0;
    for (unsigned records=0;records<RECORD_COUNT_MAX;++records) {
        enum tls13_client_result r=receive(io,record);
        if (r!=TLS13_CLIENT_OK) { return r; }
        if (!unprotect(record,keys)) { return TLS13_CLIENT_RECORD; }
        const unsigned char *payload=record->data+RECORD_HEADER;
        if (record->type==HANDSHAKE) {
            /* Ignore authenticated NewSessionTicket messages. No resumption. */
            if (!record->size || record->size>TLS13_TRANSCRIPT_MAX-buffered) { return TLS13_CLIENT_RECORD; }
            copy_bytes(pending+buffered,payload,record->size);
            buffered+=record->size;
            size_t cursor=0;
            while (buffered-cursor>=4) {
                size_t ticket=message_bytes(pending+cursor);
                if (pending[cursor]!=4 || ticket<17 || ticket>TLS13_TRANSCRIPT_MAX) { return TLS13_CLIENT_RECORD; }
                if (ticket>buffered-cursor) { break; }
                cursor+=ticket;
            }
            buffered-=cursor;
            copy_bytes(pending,pending+cursor,buffered);
            continue;
        }
        if (buffered) { return TLS13_CLIENT_RECORD; }
        if (record->type==ALERT && record->size==2 && payload[0]==1 && !payload[1]) {
            return response(context,NULL,0) ? TLS13_CLIENT_OK : TLS13_CLIENT_RESPONSE;
        }
        if (record->type!=APPLICATION) { return TLS13_CLIENT_RECORD; }
        if (record->size && !response(context,payload,record->size)) { return TLS13_CLIENT_RESPONSE; }
    }
    return TLS13_CLIENT_RECORD;
}

static enum tls13_client_result start(const struct tls13_io *io,struct record *record,unsigned char *transcript,
    size_t *length,const char *name,size_t name_bytes,struct tls13_key_schedule *secrets)
{
    unsigned char scalar[32],random[64],point[64],digest[32];
    enum tls13_client_result result=TLS13_CLIENT_RANDOM;
    bool valid=false;
    for (unsigned attempt=0;attempt<4 && !valid;++attempt) {
        if (!io->random(io->context,scalar,sizeof(scalar))) { goto done; }
        valid=p256_scalar_valid(scalar);
    }
    if (!valid || !io->random(io->context,random,sizeof(random))) { goto done; }
    p256_generator(point);
    if (!p256_multiply(scalar,point,point)) { goto done; }
    *length=client_hello(transcript,random,point,name,name_bytes);
    record_header(record,HANDSHAKE,0x0301,*length);
    copy_bytes(record->data+RECORD_HEADER,transcript,*length);
    if (!io->write_all(io->context,record->data,RECORD_HEADER+*length)) { result=TLS13_CLIENT_IO;goto done; }
    result=read_hello(io,record,transcript,length);
    if (result!=TLS13_CLIENT_OK) { goto done; }
    result=TLS13_CLIENT_HELLO;
    const unsigned char *peer=tls13_server_share(transcript,*length);
    if (!peer || !p256_multiply(scalar,peer,point)) { goto done; }
    tls13_hash(transcript,*length,digest);
    if (tls13_handshake_keys(point,digest,secrets)) { result=TLS13_CLIENT_OK; }
done:
    crypto_wipe(scalar,sizeof(scalar));
    crypto_wipe(random,sizeof(random));
    crypto_wipe(point,sizeof(point));
    return result;
}

enum tls13_client_result tls13_request(const struct tls13_io *io,const char *name,
    tls13_trust_fn trust,void *trust_context,const void *request,size_t bytes,
    tls13_response_fn response,void *response_context,enum tls13_auth_result *auth)
{
    size_t name_bytes=hostname_bytes(name),length=0;
    if (!io || !io->read_exact || !io->write_all || !io->random || !name_bytes ||
        !trust || !request || !bytes || bytes>1024 || !response || !auth) { return TLS13_CLIENT_ARGUMENT; }
    *auth=TLS13_AUTH_FORMAT;
    unsigned char transcript[TLS13_TRANSCRIPT_MAX],digest[32],finished[36]={20,0,0,32},secret[32];
    struct record record;
    struct tls13_key_schedule secrets;
    struct traffic server,client;
    enum tls13_client_result result=start(io,&record,transcript,&length,name,name_bytes,&secrets);
    if (result!=TLS13_CLIENT_OK) { goto done; }
    result=TLS13_CLIENT_RECORD;
    if (!traffic_keys(&server,secrets.server) || !traffic_keys(&client,secrets.client)) { goto done; }
    result=read_server_flight(io,&record,&server,transcript,&length);
    if (result!=TLS13_CLIENT_OK) { goto done; }
    *auth=tls13_authenticate(transcript,length,secrets.server,name,trust,trust_context);
    result=TLS13_CLIENT_AUTH;
    if (*auth!=TLS13_AUTH_OK) { goto done; }
    tls13_hash(transcript,length,digest);
    result=TLS13_CLIENT_IO;
    if (!tls13_finished(secrets.client,digest,finished+4) ||
        !send_protected(io,&record,&client,HANDSHAKE,finished,sizeof(finished))) { goto done; }
    if (!tls13_expand(secrets.master,"s ap traffic",digest,32,secret,32) || !traffic_keys(&server,secret) ||
        !tls13_expand(secrets.master,"c ap traffic",digest,32,secret,32) || !traffic_keys(&client,secret) ||
        !send_protected(io,&record,&client,APPLICATION,request,bytes)) { goto done; }
    result=read_response(io,&record,&server,transcript,response,response_context);
    if (result==TLS13_CLIENT_OK) {
        static const unsigned char close_notify[]={1,0};
        if (!send_protected(io,&record,&client,ALERT,close_notify,sizeof(close_notify))) { result=TLS13_CLIENT_IO; }
    }
done:
    crypto_wipe(&secrets,sizeof(secrets));
    crypto_wipe(&server,sizeof(server));
    crypto_wipe(&client,sizeof(client));
    crypto_wipe(secret,sizeof(secret));
    crypto_wipe(finished,sizeof(finished));
    crypto_wipe(&record,sizeof(record));
    return result;
}

struct echo { const unsigned char *message;size_t size,received; };

static bool verify_echo(void *context,const unsigned char *data,size_t bytes)
{
    struct echo *echo=context;
    if (!data) { return echo->received==echo->size; }
    if (bytes>echo->size-echo->received || !crypto_equal(data,echo->message+echo->received,bytes)) { return false; }
    echo->received+=bytes;
    return true;
}

enum tls13_client_result tls13_echo(const struct tls13_io *io,const char *name,
    tls13_trust_fn trust,void *trust_context,const void *message,size_t bytes,enum tls13_auth_result *auth)
{
    if (bytes>TLS13_ECHO_MAX) { return TLS13_CLIENT_ARGUMENT; }
    struct echo echo={message,bytes,0};
    enum tls13_client_result result=tls13_request(io,name,trust,trust_context,message,bytes,verify_echo,&echo,auth);
    return result==TLS13_CLIENT_RESPONSE ? TLS13_CLIENT_ECHO : result;
}
