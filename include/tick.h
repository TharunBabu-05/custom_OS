#ifndef OS_TICK_H
#define OS_TICK_H
#include <stdbool.h>
#include <stdint.h>
enum tick_result { TICK_OK,TICK_EARLY,TICK_INVALID,TICK_OVERFLOW };
struct tick_state {
    uint64_t frequency,period,deadline,delivered,periods,late;
    unsigned requested_hz;
};
/* Pure absolute-counter arithmetic, no register I/O or allocations.
 * The period is floor(frequency/hz) counter ticks. Thus the actual rate is
 * frequency/period, exactly hz only when the division has no remainder.
 * Counter/deadline wrapping is deliberately unsupported; failures preserve
 * output so the runtime can stop the timer rather than schedule in the past. */
bool tick_init(uint64_t frequency,unsigned hz,uint64_t now,struct tick_state *out);
/* One delivered interrupt may account for multiple elapsed periods after a
 * delay. Advance once, in O(1), to a deadline strictly later than `now`.
 * `late` counts skipped periods, not elapsed counter ticks or milliseconds. */
enum tick_result tick_advance(struct tick_state *,uint64_t now);
#endif
