#pragma once
// Pauseable UI clock measured in milliseconds. Link leases, UART timeouts and
// active-job deadlines still use wall time; only visual aging uses this clock.
// Unsigned elapsed-time arithmetic handles normal millis() wraparound.
#include <stdint.h>
namespace presentation {
// Host presentation milliseconds, not bus timestamps. Transport uses wall time.
class Clock {
    uint32_t wall_ms=0, elapsed_ms=0;
    bool initialized=false;
public:
    bool running=true;
    uint32_t now(uint32_t current_wall_ms) {
        if (!initialized) { initialized=true; wall_ms=elapsed_ms=current_wall_ms; }
        if (running) elapsed_ms+=uint32_t(current_wall_ms-wall_ms);
        wall_ms=current_wall_ms;
        return elapsed_ms;
    }
    void setRunning(bool active,uint32_t current_wall_ms) { now(current_wall_ms); running=active; }
};
}
