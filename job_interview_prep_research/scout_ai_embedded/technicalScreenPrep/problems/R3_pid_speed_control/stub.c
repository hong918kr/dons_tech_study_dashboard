/*
 * R3  Motor Speed PID with a Simulated Plant
 *
 * A DC motor speed loop runs at 100 Hz. The plant (motor) model is GIVEN:
 *     d(omega)/dt = (K*u - omega) / tau      K = 10 rad/s per V, tau = 0.5 s
 * motor_step() advances it by dt with the exact discretization.
 *
 * Part A: pid_step() -- one PID update.
 *   - u = P + I + D, then clamp to [u_min, u_max] (actuator limit, e.g. 0..12 V).
 *   - I is stored already multiplied by ki:  integ += ki * e * dt.
 *   - Anti-windup (when c->anti_windup is true): CONDITIONAL INTEGRATION. If the
 *     output would saturate AND the error pushes further into saturation, do not
 *     update the integrator on this step.
 *   - Derivative on MEASUREMENT, not on error:  D = -kd * (meas - prev_meas) / dt.
 *     On the very first call there is no previous measurement -> D = 0.
 *   - If dt <= 0 (or NaN): change no state and return the previous output.
 *
 * Part B: simulate() -- the fixed-rate loop + metrics.
 *   - For k = 0..n_steps-1:  u = pid_step(c, setpoints[k], m->omega, dt);
 *                            motor_step(m, u, dt);  log[k] = m->omega;
 *     so log[k] is the speed at time t = (k+1)*dt.
 *   - Metrics are measured on the LAST setpoint segment: let s0 be the first index
 *     such that setpoints[s0..n-1] are all equal to r = setpoints[n-1]. The step
 *     happens at t = s0*dt, from y0 = speed just before step s0 to r. All times
 *     below are measured from the step (t_sample - s0*dt).
 *       overshoot_pct  = max(0, max over segment of dir*(log - r)) / |r| * 100,
 *                        dir = +1 for a step up, -1 for a step down
 *       rise_time_s    = time of first sample >= 90 % of the step minus time of
 *                        first sample >= 10 % of the step; -1 if 90 % never reached
 *       settling_time_s= time of the first sample after which every sample stays
 *                        within +-2 % of |r|; -1 if the last sample is outside
 *       ss_error       = r - log[n-1]
 *   - If n_steps == 0, dt <= 0, or a pointer is NULL: return valid = false.
 *   - No malloc: the caller provides log[n_steps].
 *
 * Build & run:  ./run.sh R3
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <math.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)
#define NEAR(a, b, tol) (fabs((a) - (b)) <= (tol))

/* ---------------- given: plant model ---------------- */
typedef struct {
    double omega;   /* speed [rad/s] */
    double K;       /* DC gain [rad/s per V] */
    double tau;     /* time constant [s] */
} Motor;

static void motor_init(Motor *m, double omega0) { m->omega = omega0; m->K = 10.0; m->tau = 0.5; }

/* Exact discretization of d(omega)/dt = (K*u - omega)/tau for constant u over dt. */
void motor_step(Motor *m, double u_volts, double dt)
{
    m->omega += (m->K * u_volts - m->omega) * (1.0 - exp(-dt / m->tau));
}

/* ---------------- given: controller struct ---------------- */
typedef struct {
    double kp, ki, kd;        /* V per rad/s, V per rad, V per rad/s^2 */
    double u_min, u_max;      /* actuator limits [V] */
    bool   anti_windup;
    /* state */
    double integ;             /* integral term, already times ki [V] */
    double prev_meas;         /* last measurement, for D on measurement */
    bool   primed;            /* prev_meas valid? */
    double u_last;            /* last output, returned when dt is invalid */
} Pid;

static void pid_init(Pid *c, double kp, double ki, double kd,
                     double u_min, double u_max, bool anti_windup)
{
    *c = (Pid){ .kp = kp, .ki = ki, .kd = kd, .u_min = u_min, .u_max = u_max,
                .anti_windup = anti_windup };
}

typedef struct {
    bool   valid;
    double overshoot_pct;
    double rise_time_s;       /* -1 = never reached 90 % */
    double settling_time_s;   /* -1 = not settled at the end */
    double ss_error;
} SimResult;

/* ---------------- Part A ---------------- */
double pid_step(Pid *c, double setpoint, double measured, double dt)
{
    /* TODO: your code here */
    (void)c; (void)setpoint; (void)measured; (void)dt;
    return 0.0;
}

/* ---------------- Part B ---------------- */
SimResult simulate(Pid *c, Motor *m, const double *setpoints, size_t n_steps,
                   double dt, double *log)
{
    /* TODO: your code here (call pid_step + motor_step in a loop, then scan log) */
    (void)c; (void)m; (void)setpoints; (void)n_steps; (void)dt; (void)log;
    SimResult res = { .valid = false };
    return res;
}

/* ---------------- tests ---------------- */
#define DT    0.01        /* 100 Hz */
#define NMAX  600

int main(void)
{
    static double sp[NMAX], lg[NMAX];
    Pid c; Motor m; SimResult r, r_aw;

    /* 1. P-only, step 0 -> 60 rad/s for 3 s: steady-state error stays at 10 rad/s */
    for (size_t i = 0; i < 300; i++) sp[i] = 60.0;
    pid_init(&c, 0.5, 0.0, 0.0, 0.0, 12.0, true); motor_init(&m, 0.0);
    r = simulate(&c, &m, sp, 300, DT, lg);
    CHECK(r.valid);
    CHECK(NEAR(r.ss_error, 10.0, 0.01));              /* omega_ss = 60 * 5/6 = 50 */
    CHECK(r.overshoot_pct == 0.0);
    CHECK(r.rise_time_s == -1.0);                     /* 90 % of 60 = 54 never reached */
    CHECK(r.settling_time_s == -1.0);

    /* 2. PI removes the error; without anti-windup it overshoots ~25.6 % */
    pid_init(&c, 0.5, 2.0, 0.0, 0.0, 12.0, false); motor_init(&m, 0.0);
    r = simulate(&c, &m, sp, 300, DT, lg);
    pid_init(&c, 0.5, 2.0, 0.0, 0.0, 12.0, true);  motor_init(&m, 0.0);
    r_aw = simulate(&c, &m, sp, 300, DT, lg);
    CHECK(fabs(r.ss_error) < 0.01 && fabs(r_aw.ss_error) < 0.01);
    CHECK(NEAR(r.overshoot_pct, 25.56, 0.05));
    CHECK(r_aw.overshoot_pct < 0.1);
    CHECK(NEAR(r_aw.rise_time_s, 0.34, 1e-9));
    CHECK(r_aw.settling_time_s > 0.0 && r_aw.settling_time_s < r.settling_time_s);

    /* 3. windup demo: 150 rad/s for 0.5 s is unreachable (12 V * 10 = 120 max),
     *    then 100 rad/s for 5.5 s. Metrics are on the 100 rad/s segment. */
    for (size_t i = 0; i < NMAX; i++) sp[i] = (i < 50) ? 150.0 : 100.0;
    pid_init(&c, 0.5, 2.0, 0.0, 0.0, 12.0, false); motor_init(&m, 0.0);
    r = simulate(&c, &m, sp, NMAX, DT, lg);
    pid_init(&c, 0.5, 2.0, 0.0, 0.0, 12.0, true);  motor_init(&m, 0.0);
    r_aw = simulate(&c, &m, sp, NMAX, DT, lg);
    CHECK(NEAR(r.overshoot_pct, 19.93, 0.01));        /* wound-up I keeps u at 12 V */
    CHECK(r_aw.overshoot_pct < 0.01);
    CHECK(NEAR(r.settling_time_s, 3.81, 1e-9));
    CHECK(NEAR(r_aw.settling_time_s, 0.52, 1e-9));

    /* 4. derivative on measurement: a setpoint step gives no kick */
    pid_init(&c, 0.0, 0.0, 1.0, -1000.0, 1000.0, false);
    CHECK(pid_step(&c, 0.0, 10.0, DT) == 0.0);        /* first call: D = 0 */
    CHECK(pid_step(&c, 100.0, 10.0, DT) == 0.0);      /* setpoint jumps, meas flat */
    CHECK(NEAR(pid_step(&c, 100.0, 11.0, DT), -100.0, 1e-9));   /* -kd * 1 / 0.01 */

    /* 5. output clamped; anti-windup freezes I while saturated */
    pid_init(&c, 1.0, 1.0, 0.0, 0.0, 12.0, true);
    int n_sat = 0;
    for (int i = 0; i < 10; i++) n_sat += (pid_step(&c, 100.0, 0.0, DT) == 12.0);
    CHECK(n_sat == 10);
    CHECK(c.integ == 0.0);
    CHECK(pid_step(&c, 0.0, 100.0, DT) == 0.0);       /* clamped low */
    pid_init(&c, 1.0, 1.0, 0.0, 0.0, 12.0, false);
    for (int i = 0; i < 10; i++) (void)pid_step(&c, 100.0, 0.0, DT);
    CHECK(NEAR(c.integ, 10.0, 1e-9));                 /* 10 * 1 * 100 * 0.01: wound up */

    /* 6. dt == 0 / negative: no state change, previous output returned */
    pid_init(&c, 0.1, 1.0, 0.0, 0.0, 12.0, true);
    double u1 = pid_step(&c, 50.0, 0.0, DT);
    double integ1 = c.integ;
    CHECK(pid_step(&c, 50.0, 0.0, 0.0) == u1);
    CHECK(pid_step(&c, 50.0, 0.0, -DT) == u1);
    CHECK(c.integ == integ1);

    /* 7. simulate guards */
    motor_init(&m, 0.0);
    CHECK(!simulate(&c, &m, sp, 0, DT, lg).valid);
    CHECK(!simulate(&c, &m, sp, 10, 0.0, lg).valid);
    CHECK(!simulate(&c, &m, sp, 10, DT, NULL).valid);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
