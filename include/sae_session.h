#ifndef OS_SAE_SESSION_H
#define OS_SAE_SESSION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define SAE_COMMIT_BYTES 98
#define SAE_CONFIRM_BYTES 34
enum sae_phase { SAE_EMPTY,SAE_COMMITTED,SAE_KEYS,SAE_SENT,SAE_VERIFIED,SAE_EXPORTED,SAE_FAILED };
enum sae_result { SAE_OK,SAE_ARGUMENT,SAE_STATE,SAE_RANDOM,SAE_FORMAT,SAE_REFLECTION,SAE_MATH,SAE_CONFIRM };
struct sae_keys { unsigned char pmk[32],pmkid[16]; };
/* Narrow experimental group-19 / ordinary SAE AKM-8 profile. No optional
 * IEs, rejected groups, password identifier negotiation, SAE-PK or radio I/O.
 * Initialize to zero; clear before reuse. Inputs/outputs must not overlap this
 * state. The caller's RNG must supply fresh cryptographic bytes and fail on
 * source errors. Deterministic callbacks are only for PUBLIC test fixtures. */
struct sae_session {
    enum sae_phase phase;enum sae_result error;unsigned random_pairs;
    unsigned char pwe[64],random[32],own[SAE_COMMIT_BYTES],peer[SAE_COMMIT_BYTES];
    unsigned char kck[32],confirm[SAE_CONFIRM_BYTES];struct sae_keys keys;
};
typedef bool (*sae_random_fn)(void *,unsigned char output[32]);
void sae_session_clear(struct sae_session *);
enum sae_result sae_session_start(struct sae_session *,const unsigned char pwe[64],sae_random_fn,void *);
enum sae_result sae_session_commit(const struct sae_session *,unsigned char output[SAE_COMMIT_BYTES]);
enum sae_result sae_session_peer(struct sae_session *,const void *body,size_t bytes);
enum sae_result sae_session_confirm(struct sae_session *,unsigned char output[SAE_CONFIRM_BYTES]);
enum sae_result sae_session_verify(struct sae_session *,const void *body,size_t bytes);
/* Export once, only after own confirm generated + peer confirm verified.
 * Destroys all session material. Wrong-state export clears caller output. */
enum sae_result sae_session_take_keys(struct sae_session *,struct sae_keys *);
bool sae_session_selftest(void);
#endif
