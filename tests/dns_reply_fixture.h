/* Model response builder, independent of the resolver. Public IPv4 only. */
#ifndef TEST_DNS_REPLY_FIXTURE_H
#define TEST_DNS_REPLY_FIXTURE_H
#include <string.h>
#include <stddef.h>
static unsigned fixture_sum(const unsigned char *p,size_t n)
{ unsigned s=0;for (size_t i=0;i<n;i+=2) { s+=(unsigned)p[i]<<8|(i+1<n ? p[i+1] : 0); }while (s>>16) { s=(s&65535)+(s>>16); }return (~s)&65535; }
static void fixture_put16(unsigned char *p,unsigned n) { p[0]=(unsigned char)(n>>8);p[1]=(unsigned char)n; }
static size_t dns_reply_fixture(const void *frame,unsigned char out[600],unsigned rcode)
{
    const unsigned char *q=frame;size_t dn=((size_t)q[38]<<8|q[39])-8,n=42+dn+(rcode ? 0 : 16);memset(out,0,600);memcpy(out,q,42+dn);
    memcpy(out,q+6,6);memcpy(out+6,q,6);memcpy(out+26,q+30,4);memcpy(out+30,q+26,4);memcpy(out+34,q+36,2);memcpy(out+36,q+34,2);
    out[44]=0x81;out[45]=(unsigned char)(0x80|rcode);out[49]=(unsigned char)(rcode ? 0 : 1);
    if (!rcode) { const unsigned char a[]={0xc0,12,0,1,0,1,0,0,0,60,0,4,203,0,113,80};memcpy(out+42+dn,a,16); }
    fixture_put16(out+16,(unsigned)n-14);fixture_put16(out+38,(unsigned)n-34);out[24]=out[25]=out[40]=out[41]=0;
    fixture_put16(out+24,fixture_sum(out+14,20));unsigned char pseudo[600];memcpy(pseudo,out+26,8);pseudo[8]=0;pseudo[9]=17;memcpy(pseudo+10,out+38,2);memcpy(pseudo+12,out+34,n-34);
    unsigned sum=fixture_sum(pseudo,12+n-34);fixture_put16(out+40,sum ? sum : 65535);return n;
}
#endif
