/*
Implement an FSM that handles timeout events.
Timer variable, system tick.
Recovery logic from a non-responsive state.
*/

#include <stdio.h>
#include <stdbool.h>

// FSM states
typedef enum {
    STATE_IDLE,
    STATE_ACTIVE,
    STATE_TIMEOUT,
    STATE_MAX
} fsm_state_t;

// FSM structure
typedef struct {
    fsm_state_t state;
    int timer;
    int timeout_limit;
} fsm_t;

// Initialize FSM
void fsm_init(fsm_t *fsm, int timeout_limit) {
    fsm->state = STATE_IDLE;
    fsm->timer = 0;
    fsm->timeout_limit = timeout_limit;
}

// Call this function every tick with event (1: activate, 0: no event)
void fsm_update(fsm_t *fsm, int event) {
    switch (fsm->state) {
        case STATE_IDLE:
            if (event == 1) {
                fsm->state = STATE_ACTIVE;
                fsm->timer = 0;
                printf("FSM: Enter ACTIVE\n");
            }
            break;
        case STATE_ACTIVE:
            if (event == 0) {
                fsm->timer++;
                if (fsm->timer >= fsm->timeout_limit) {
                    fsm->state = STATE_TIMEOUT;
                    printf("FSM: TIMEOUT occurred\n");
                }
            } else {
                fsm->timer = 0; // Reset timer on activity
            }
            break;
        case STATE_TIMEOUT:
            if (event == 1) {
                fsm->state = STATE_ACTIVE;
                fsm->timer = 0;
                printf("FSM: Recovered to ACTIVE\n");
            }
            break;
    }
}

// --- Test code ---
const char* fsm_state_str(fsm_state_t state) {
    switch (state) {
        case STATE_IDLE: return "IDLE";
        case STATE_ACTIVE: return "ACTIVE";
        case STATE_TIMEOUT: return "TIMEOUT";
        default: return "UNKNOWN";
    }
}

int main(void) {
    fsm_t fsm;
    fsm_init(&fsm, 3); // Timeout after 3 ticks of inactivity

    // Simulated event sequence: 1=activate, 0=no event
    int event_sequence[] = {0, 1, 0, 0, 0, 1, 0, 0, 0, 0};
    int len = sizeof(event_sequence) / sizeof(event_sequence[0]);

    printf("Tick | Event | State   | Timer\n");
    printf("-------------------------------\n");
    for (int i = 0; i < len; ++i) {
        fsm_update(&fsm, event_sequence[i]);
        printf("%4d |  %d   | %-7s| %d\n", i, event_sequence[i], fsm_state_str(fsm.state), fsm.timer);
    }
    return 0;
}

/*
------------------ Example Input/Output ------------------

Tick | Event | State   | Timer
-------------------------------
   0 |  0   | IDLE    | 0
   1 |  1   | ACTIVE  | 0
   2 |  0   | ACTIVE  | 1
   3 |  0   | ACTIVE  | 2
   4 |  0   | TIMEOUT | 3
   5 |  1   | ACTIVE  | 0
   6 |  0   | ACTIVE  | 1
   7 |  0   | ACTIVE  | 2
   8 |  0   | TIMEOUT | 3
   9 |  0   | TIMEOUT | 3

----------------------------------------------------------
*/