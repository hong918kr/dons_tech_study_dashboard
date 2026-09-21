/*
Implement traffic light control logic as an FSM.
Timers, state transition logic.
Basic example of a real-time control system.
*/

#include <stdio.h>

// Traffic light states
typedef enum {
    TL_RED,
    TL_GREEN,
    TL_YELLOW,
    TL_MAX
} tl_state_t;

// Traffic light FSM structure
typedef struct {
    tl_state_t state;
    int timer;         // Current timer value
    int red_time;      // Duration for RED
    int green_time;    // Duration for GREEN
    int yellow_time;   // Duration for YELLOW
} traffic_light_fsm_t;

// Initialize FSM
void tl_fsm_init(traffic_light_fsm_t *fsm, int red_time, int green_time, int yellow_time) {
    fsm->state = TL_RED;
    fsm->timer = red_time;
    fsm->red_time = red_time;
    fsm->green_time = green_time;
    fsm->yellow_time = yellow_time;
}

// Call this function every tick to update FSM
void tl_fsm_update(traffic_light_fsm_t *fsm) {
    if (--fsm->timer <= 0) {
        switch (fsm->state) {
            case TL_RED:
                fsm->state = TL_GREEN;
                fsm->timer = fsm->green_time;
                break;
            case TL_GREEN:
                fsm->state = TL_YELLOW;
                fsm->timer = fsm->yellow_time;
                break;
            case TL_YELLOW:
                fsm->state = TL_RED;
                fsm->timer = fsm->red_time;
                break;
            default:
                fsm->state = TL_RED;
                fsm->timer = fsm->red_time;
                break;
        }
    }
}

// --- Test code ---
const char* tl_state_str(tl_state_t state) {
    switch (state) {
        case TL_RED: return "RED";
        case TL_GREEN: return "GREEN";
        case TL_YELLOW: return "YELLOW";
        default: return "UNKNOWN";
    }
}

int main(void) {
    traffic_light_fsm_t fsm;
    tl_fsm_init(&fsm, 3, 2, 1); // RED:3 ticks, GREEN:2 ticks, YELLOW:1 tick

    printf("Tick | State  | Timer\n");
    printf("---------------------\n");
    for (int tick = 0; tick < 10; ++tick) {
        printf("%4d | %-6s | %d\n", tick, tl_state_str(fsm.state), fsm.timer);
        tl_fsm_update(&fsm);
    }
    return 0;
}

/*
------------------ Example Input/Output ------------------

Tick | State  | Timer
---------------------
   0 | RED    | 3
   1 | RED    | 2
   2 | RED    | 1
   3 | GREEN  | 2
   4 | GREEN  | 1
   5 | YELLOW | 1
   6 | RED    | 3
   7 | RED    | 2
   8 | RED    | 1
   9 | GREEN  | 2

----------------------------------------------------------
*/