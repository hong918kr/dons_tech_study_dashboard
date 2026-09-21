/*
Implement an FSM with Guard conditions.
Checking conditions for state transitions.
Providing different transition paths for the same event.
*/

#include <stdio.h>
#include <stdbool.h>

// FSM states
typedef enum {
    STATE_IDLE,
    STATE_RUN,
    STATE_ERROR,
    STATE_MAX
} fsm_state_t;

// FSM structure
typedef struct {
    fsm_state_t state;
    int value; // Example variable for guard condition
} fsm_t;

// Entry actions
void entry_idle(void)   { printf("Enter IDLE\n"); }
void entry_run(void)    { printf("Enter RUN\n"); }
void entry_error(void)  { printf("Enter ERROR\n"); }

// State transition with guard conditions
void fsm_update(fsm_t *fsm, int event) {
    // event: 0 = try to run, 1 = reset
    switch (fsm->state) {
        case STATE_IDLE:
            if (event == 0) {
                if (fsm->value > 0) { // Guard condition
                    fsm->state = STATE_RUN;
                    entry_run();
                } else {
                    fsm->state = STATE_ERROR;
                    entry_error();
                }
            }
            break;
        case STATE_RUN:
            if (event == 1) {
                fsm->state = STATE_IDLE;
                entry_idle();
            }
            break;
        case STATE_ERROR:
            if (event == 1) {
                fsm->state = STATE_IDLE;
                entry_idle();
            }
            break;
    }
}

// --- Test code ---
int main(void) {
    fsm_t fsm;
    fsm.state = STATE_IDLE;
    fsm.value = 0;

    printf("Test 1: value = 0 (should go to ERROR)\n");
    fsm_update(&fsm, 0); // Try to run, but value=0, should go to ERROR

    printf("Test 2: reset from ERROR\n");
    fsm_update(&fsm, 1); // Reset, should go to IDLE

    printf("Test 3: value = 5 (should go to RUN)\n");
    fsm.value = 5;
    fsm_update(&fsm, 0); // Try to run, value=5, should go to RUN

    printf("Test 4: reset from RUN\n");
    fsm_update(&fsm, 1); // Reset, should go to IDLE

    return 0;
}

/*
------------------ Example Input/Output ------------------

Test 1: value = 0 (should go to ERROR)
Enter ERROR
Test 2: reset from ERROR
Enter IDLE
Test 3: value = 5 (should go to RUN)
Enter RUN
Test 4: reset from RUN
Enter IDLE
*/