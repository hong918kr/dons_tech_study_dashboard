/*
Implement an FSM with Entry/Exit actions.
Calling specific functions on state transition.
Automating state initialization and cleanup tasks.
*/

#include <stdio.h>

// FSM states
typedef enum {
    STATE_IDLE,
    STATE_RUN,
    STATE_STOP,
    STATE_MAX
} fsm_state_t;

// Entry/Exit action function pointer types
typedef void (*action_func_t)(void);

// FSM structure
typedef struct {
    fsm_state_t state;
    fsm_state_t prev_state;
    action_func_t entry_actions[STATE_MAX];
    action_func_t exit_actions[STATE_MAX];
} fsm_t;

// Entry actions
void entry_idle(void)   { printf("Enter IDLE\n"); }
void entry_run(void)    { printf("Enter RUN\n"); }
void entry_stop(void)   { printf("Enter STOP\n"); }

// Exit actions
void exit_idle(void)    { printf("Exit IDLE\n"); }
void exit_run(void)     { printf("Exit RUN\n"); }
void exit_stop(void)    { printf("Exit STOP\n"); }

// Initialize FSM
void fsm_init(fsm_t *fsm) {
    fsm->state = STATE_IDLE;
    fsm->prev_state = STATE_IDLE;
    fsm->entry_actions[STATE_IDLE] = entry_idle;
    fsm->entry_actions[STATE_RUN]  = entry_run;
    fsm->entry_actions[STATE_STOP] = entry_stop;
    fsm->exit_actions[STATE_IDLE]  = exit_idle;
    fsm->exit_actions[STATE_RUN]   = exit_run;
    fsm->exit_actions[STATE_STOP]  = exit_stop;
    fsm->entry_actions[fsm->state]();
}

// State transition with entry/exit actions
void fsm_transition(fsm_t *fsm, fsm_state_t next_state) {
    if (fsm->state != next_state) {
        fsm->exit_actions[fsm->state]();
        fsm->prev_state = fsm->state;
        fsm->state = next_state;
        fsm->entry_actions[fsm->state]();
    }
}

// Simulate FSM transitions based on input
void fsm_update(fsm_t *fsm, int input) {
    // Example: 0 = idle, 1 = run, 2 = stop
    if (input >= 0 && input < STATE_MAX) {
        fsm_transition(fsm, (fsm_state_t)input);
    } else {
        printf("Invalid input: %d\n", input);
    }
}

// --- Test code ---
int main(void) {
    fsm_t fsm;
    fsm_init(&fsm);

    int input_sequence[] = {1, 2, 0, 2, 1};
    int len = sizeof(input_sequence) / sizeof(input_sequence[0]);

    printf("Input | Entry/Exit Actions\n");
    printf("-------------------------\n");
    for (int i = 0; i < len; ++i) {
        printf("  %d   | ", input_sequence[i]);
        fsm_update(&fsm, input_sequence[i]);
    }
    return 0;
}

/*
------------------ Example Input/Output ------------------

Input | Entry/Exit Actions
-------------------------
Enter IDLE
  1   | Exit IDLE
        Enter RUN
  2   | Exit RUN
        Enter STOP
  0   | Exit STOP
        Enter IDLE
  2   | Exit IDLE
        Enter STOP
  1   | Exit STOP
        Enter RUN
*/