#ifndef OS_AES_GCM_H
#define OS_AES_GCM_H
#include <stdbool.h>
#include <stddef.h>

/* In-place GCM with a 96-bit nonce and 128-bit tag. Limit each input to
 * 16,640 bytes. Open checks the tag before modifying the ciphertext. */
bool aes128_gcm_seal(const unsigned char key[16],const unsigned char nonce[12],
    const void *aad,size_t aad_bytes,void *data,size_t bytes,unsigned char tag[16]);
bool aes128_gcm_open(const unsigned char key[16],const unsigned char nonce[12],
    const void *aad,size_t aad_bytes,void *data,size_t bytes,const unsigned char tag[16]);
#endif
