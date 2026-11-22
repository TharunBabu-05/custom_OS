#include "tls13_keys.h"
#include "crypto.h"

void tls13_hash(const void *data,size_t bytes,unsigned char digest[32])
{
    struct sha256 hash;
    sha256_init(&hash);
    sha256_update(&hash,data,bytes);
    sha256_final(&hash,digest);
}

bool tls13_expand(const unsigned char secret[32],const char *label,
    const void *context,size_t context_bytes,void *out,size_t bytes)
{
    unsigned char info[3+6+16+1+32]={0,0,0,'t','l','s','1','3',' '};
    if (!label || context_bytes>32 || (!context && context_bytes) || bytes>32) { return false; }
    size_t label_bytes=0;
    while (label[label_bytes]) {
        if (label_bytes==16) { return false; }
        info[9+label_bytes]=(unsigned char)label[label_bytes];
        ++label_bytes;
    }
    if (!label_bytes) { return false; }
    info[1]=(unsigned char)bytes;
    info[2]=(unsigned char)(6+label_bytes);
    size_t at=9+label_bytes;
    info[at++]=(unsigned char)context_bytes;
    for (size_t i=0;i<context_bytes;++i) { info[at++]=((const unsigned char *)context)[i]; }
    return hkdf_sha256_expand(secret,info,at,out,bytes);
}

bool tls13_finished(const unsigned char secret[32],const unsigned char digest[32],unsigned char out[32])
{
    unsigned char key[32];
    const struct crypto_span message={digest,32};
    bool valid=tls13_expand(secret,"finished",NULL,0,key,sizeof(key)) &&
        hmac_sha256(key,sizeof(key),&message,1,out);
    crypto_wipe(key,sizeof(key));
    return valid;
}

bool tls13_handshake_keys(const unsigned char shared[32],const unsigned char hello_hash[32],struct tls13_key_schedule *out)
{
    unsigned char zero[32]={0},empty[32],early[32],derived[32],handshake[32];
    const struct crypto_span missing={zero,sizeof(zero)},ecdh={shared,32};
    tls13_hash("",0,empty);
    bool valid=hmac_sha256(zero,sizeof(zero),&missing,1,early) &&
        tls13_expand(early,"derived",empty,sizeof(empty),derived,sizeof(derived)) &&
        hmac_sha256(derived,sizeof(derived),&ecdh,1,handshake) &&
        tls13_expand(handshake,"c hs traffic",hello_hash,32,out->client,32) &&
        tls13_expand(handshake,"s hs traffic",hello_hash,32,out->server,32) &&
        tls13_expand(handshake,"derived",empty,sizeof(empty),derived,sizeof(derived)) &&
        hmac_sha256(derived,sizeof(derived),&missing,1,out->master);
    crypto_wipe(early,sizeof(early));
    crypto_wipe(derived,sizeof(derived));
    crypto_wipe(handshake,sizeof(handshake));
    if (!valid) { crypto_wipe(out,sizeof(*out)); }
    return valid;
}
