/* Original bounded periodic deadline arithmetic. */
#include "tick.h"
bool tick_init(uint64_t frequency,unsigned hz,uint64_t now,struct tick_state *out)
{
    if (!out || !hz || hz>1000 || frequency<hz) { return false; }
    uint64_t period=frequency/hz;
    if (!period || period>INT64_MAX || period>UINT64_MAX-now) { return false; }
    *out=(struct tick_state){.frequency=frequency,.period=period,.deadline=now+period,.requested_hz=hz};return true;
}
enum tick_result tick_advance(struct tick_state *s,uint64_t now)
{
    if (!s || !s->requested_hz || s->requested_hz>1000 || !s->period || s->period>INT64_MAX ||
        s->frequency/s->requested_hz!=s->period || s->deadline<s->period ||
        s->periods<s->delivered || s->late!=s->periods-s->delivered) { return TICK_INVALID; }
    if (now<s->deadline) { return TICK_EARLY; }
    uint64_t skipped=(now-s->deadline)/s->period;
    if (skipped==UINT64_MAX) { return TICK_OVERFLOW; }
    uint64_t elapsed=skipped+1;
    if (elapsed>(UINT64_MAX-s->deadline)/s->period || s->delivered==UINT64_MAX ||
        elapsed>UINT64_MAX-s->periods || skipped>UINT64_MAX-s->late) { return TICK_OVERFLOW; }
    s->deadline+=elapsed*s->period;++s->delivered;s->periods+=elapsed;s->late+=skipped;return TICK_OK;
}
