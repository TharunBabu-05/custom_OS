#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aes_gcm.h"
#include "host_assert.h"
#include "tls13_client.h"
#include "trust_time.h"

struct client {
    SOCKET socket;
    bool corrupt_records,corrupt_payload,no_entropy;
};

static bool socket_read(void *context,void *data,size_t length)
{
    struct client *client=context;
    char *p=data;
    for (size_t bytes=length;bytes;) {
        int n=recv(client->socket,p,bytes>7 ? 7 : (int)bytes,0);
        if (n<=0) { return false; }
        p+=n;bytes-=(size_t)n;
    }
    if (client->corrupt_payload) {
        p[-1]^=1;
        client->corrupt_payload=false;
    } else if (client->corrupt_records && length==5 && ((unsigned char *)data)[0]==23) {
        client->corrupt_payload=true;
    }
    return true;
}

static bool socket_write(void *context,const void *data,size_t bytes)
{
    struct client *client=context;
    const char *p=data;
    while (bytes) {
        int n=send(client->socket,p,bytes>13 ? 13 : (int)bytes,0);
        if (n<=0) { return false; }
        p+=n;bytes-=(size_t)n;
    }
    return true;
}

static bool random_bytes(void *context,void *data,size_t bytes)
{
    const struct client *client=context;
    return !client->no_entropy && BCryptGenRandom(NULL,data,(ULONG)bytes,BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0;
}

static bool pin_matches(const struct pinned_cert *cert,void *pin)
{ return memcmp(pin,cert->digest,32)==0; }

static void gcm_vectors(void)
{
    const unsigned char key[16]={0},nonce[12]={0};
    const unsigned char ciphertext[16]={
        0x03,0x88,0xda,0xce,0x60,0xb6,0xa3,0x92,0xf3,0x28,0xc2,0xb9,0x71,0xb2,0xfe,0x78
    };
    const unsigned char expected[16]={
        0xab,0x6e,0x47,0xd4,0x2c,0xec,0x13,0xbd,0xf5,0x3a,0x67,0xb2,0x12,0x57,0xbd,0xdf
    };
    unsigned char tag[16],data[16]={0};
    assert(aes128_gcm_seal(key,nonce,NULL,0,data,sizeof(data),tag));
    assert(!memcmp(data,ciphertext,sizeof(data)) && !memcmp(tag,expected,sizeof(tag)));
    tag[0]^=1;
    assert(!aes128_gcm_open(key,nonce,NULL,0,data,sizeof(data),tag));
    assert(!memcmp(data,ciphertext,sizeof(data)));
    tag[0]^=1;
    assert(aes128_gcm_open(key,nonce,NULL,0,data,sizeof(data),tag));
    assert(!memcmp(data,key,sizeof(data)));
}

int main(int argc,char **argv)
{
    gcm_vectors();
    assert(argc==3);
    unsigned char pin[32];
    assert(trust_parse_digest(argv[2],pin));
    const int port=atoi(argv[1]);
    WSADATA startup;
    assert(WSAStartup(MAKEWORD(2,2),&startup)==0);
    static const unsigned char message[]="ouros-tls";
    for (unsigned mode=0;mode<5;++mode) {
        struct client client={socket(AF_INET,SOCK_STREAM,IPPROTO_TCP),mode==1,false,mode==0};
        assert(client.socket!=INVALID_SOCKET);
        DWORD timeout=20000;
        int enable=1;
        assert(!setsockopt(client.socket,SOL_SOCKET,SO_RCVTIMEO,(const char *)&timeout,sizeof(timeout)));
        assert(!setsockopt(client.socket,SOL_SOCKET,SO_SNDTIMEO,(const char *)&timeout,sizeof(timeout)));
        assert(!setsockopt(client.socket,IPPROTO_TCP,TCP_NODELAY,(const char *)&enable,sizeof(enable)));
        struct sockaddr_in peer={0};
        peer.sin_family=AF_INET;
        peer.sin_port=htons((u_short)port);
        peer.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        assert(!connect(client.socket,(struct sockaddr *)&peer,sizeof(peer)));
        const struct tls13_io io={&client,socket_read,socket_write,random_bytes};
        enum tls13_auth_result auth;
        if (mode==3) { pin[0]^=1; }
        enum tls13_client_result result=tls13_echo(&io,mode==2 ? "wrong.ouros.test" : "demo.ouros.test",
            pin_matches,pin,message,sizeof(message)-1,&auth);
        if (mode==3) { pin[0]^=1; }
        if ((mode==0 && result!=TLS13_CLIENT_RANDOM) || (mode==1 && result!=TLS13_CLIENT_RECORD) ||
            (mode==2 && (result!=TLS13_CLIENT_AUTH || auth!=TLS13_AUTH_NAME)) ||
            (mode==3 && (result!=TLS13_CLIENT_AUTH || auth!=TLS13_AUTH_TRUST)) ||
            (mode==4 && (result!=TLS13_CLIENT_OK || auth!=TLS13_AUTH_OK))) {
            fprintf(stderr,"mode=%u result=%u auth=%u Winsock=%d\n",mode,result,auth,WSAGetLastError());
            return 1;
        }
        closesocket(client.socket);
    }
    WSACleanup();
    puts("PASS: AES-GCM vector, live OpenSSL TLS 1.3 echo, RNG failure, record tampering, wrong name and wrong pin.");
    return 0;
}
