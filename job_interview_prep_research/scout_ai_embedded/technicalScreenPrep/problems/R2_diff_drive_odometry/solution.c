/*
 * R2  Wheel Odometry and Waypoint Following (differential drive)
 *
 * A two-wheeled ground robot has one quadrature encoder per wheel. Each encoder is a
 * 16-bit hardware counter that wraps in both directions.
 *
 * Part A  enc_delta(prev, now): signed tick change between two counter reads.
 *         Must be correct across the 65535 <-> 0 wrap. Assume |real motion| < 32768
 *         ticks between reads.
 * Part B  wrap_angle(a): map any angle to (-pi, pi].
 *         odom_update(p, dl_ticks, dr_ticks, r): dead-reckon the pose from the two wheel
 *         deltas. ticks -> meters, d = (dl + dr) / 2, dtheta = (dr - dl) / track.
 *         Use midpoint (or exact-arc) integration, not Euler. Keep theta in (-pi, pi].
 * Part C  follow_waypoints(...): fixed-step simulation (unicycle model, constant v).
 *         Each step: if within reach_tol of the current waypoint, advance to the next
 *         (repeat); when all are reached return true and write the time to *t_done.
 *         Otherwise steer with omega = k_heading * wrap(bearing_to_wp - theta),
 *         clamp |omega| <= r->max_omega_rad_s, and move one step of dt.
 *         Time is k * dt (step k). Return false (t_done untouched) once t >= t_max.
 *         n == 0 -> true, t_done = 0.
 *
 * Reference solution.  Build & run:  ./run.sh R2 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <math.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)
#define NEAR(a, b, tol) (fabs((a) - (b)) <= (tol))

#define PI 3.14159265358979323846

typedef struct { double x, y, theta; } Pose;      /* meters, meters, radians (-pi, pi] */
typedef struct { double x, y; } Point;
typedef struct {
    uint32_t ticks_per_rev;      /* encoder ticks per wheel revolution (after x4 decode) */
    double   wheel_radius_m;
    double   track_width_m;      /* distance between the two wheel contact points */
    double   max_omega_rad_s;    /* turn-rate limit used by follow_waypoints */
} Robot;

/* ---- Part A ---- */
int32_t enc_delta(uint16_t prev, uint16_t now)
{
    /* modulo-2^16 difference, then reinterpret as signed: same trick as wrap-safe timers */
    return (int16_t)(uint16_t)(now - prev);
}

/* ---- Part B ---- */
double wrap_angle(double a)
{
    a = fmod(a, 2.0 * PI);                 /* (-2pi, 2pi) */
    if (a > PI)        a -= 2.0 * PI;
    else if (a <= -PI) a += 2.0 * PI;
    return a;                              /* (-pi, pi] */
}

/* Move along a constant-curvature arc of length d that turns by dth.
 * The chord points along the midpoint heading; its length is d * sin(h)/h, h = dth/2.
 * (Plain midpoint uses length d: error O(d*dth^2) per step. This is exact.) */
static void advance(Pose *p, double d, double dth)
{
    double h = 0.5 * dth;
    double chord = (fabs(h) < 1e-9) ? d : d * sin(h) / h;
    p->x += chord * cos(p->theta + h);
    p->y += chord * sin(p->theta + h);
    p->theta = wrap_angle(p->theta + dth);
}

void odom_update(Pose *p, int32_t dl_ticks, int32_t dr_ticks, const Robot *r)
{
    double m_per_tick = 2.0 * PI * r->wheel_radius_m / (double)r->ticks_per_rev;
    double dl = dl_ticks * m_per_tick;
    double dr = dr_ticks * m_per_tick;
    advance(p, 0.5 * (dl + dr), (dr - dl) / r->track_width_m);
}

/* ---- Part C ---- */
bool follow_waypoints(Pose *p, const Point *wps, size_t n, const Robot *r,
                      double v, double k_heading, double dt, double reach_tol,
                      double t_max, double *t_done)
{
    if (dt <= 0.0 || (n > 0 && wps == NULL)) return false;
    size_t i = 0;
    for (uint32_t k = 0;; k++) {
        double t = k * dt;                          /* no float accumulation drift */
        while (i < n && hypot(wps[i].x - p->x, wps[i].y - p->y) <= reach_tol)
            i++;                                    /* may skip several at once */
        if (i == n) { *t_done = t; return true; }
        if (t >= t_max) return false;

        double bearing = atan2(wps[i].y - p->y, wps[i].x - p->x);
        double w = k_heading * wrap_angle(bearing - p->theta);   /* shortest way round */
        if (w >  r->max_omega_rad_s) w =  r->max_omega_rad_s;
        if (w < -r->max_omega_rad_s) w = -r->max_omega_rad_s;
        advance(p, v * dt, w * dt);
    }
}

int main(void)
{
    const Robot rb = { 2048, 0.10, 0.50, 2.0 };     /* 1 rev = 2*pi*0.1 = 0.628319 m */
    Pose p;
    double t;

    /* 1. enc_delta, no wrap */
    CHECK(enc_delta(100, 150) == 50);
    CHECK(enc_delta(150, 100) == -50);

    /* 2. enc_delta across the wrap, both directions */
    CHECK(enc_delta(65530, 4) == 10);
    CHECK(enc_delta(3, 65533) == -6);

    /* 3. half-range limit: +32767 is fine, +32769 aliases to -32767 */
    CHECK(enc_delta(0, 32767) == 32767);
    CHECK(enc_delta(0, 32769) == -32767);

    /* 4. straight line: both wheels one revolution */
    p = (Pose){ 0, 0, 0 };
    odom_update(&p, 2048, 2048, &rb);
    CHECK(NEAR(p.x, 0.628319, 1e-6) && NEAR(p.y, 0.0, 1e-12) && NEAR(p.theta, 0.0, 1e-12));

    /* 5. spin in place: x, y unchanged, theta = 2 * 0.314159 / 0.5 = 1.256637 */
    p = (Pose){ 1, 2, 0 };
    odom_update(&p, -1024, 1024, &rb);
    CHECK(NEAR(p.x, 1.0, 1e-12) && NEAR(p.y, 2.0, 1e-12) && NEAR(p.theta, 1.256637, 1e-6));

    /* 6. quarter circle, radius 0.5 m: 1280 small steps of (1, 3) ticks -> (0.5, 0.5, pi/2) */
    p = (Pose){ 0, 0, 0 };
    for (int s = 0; s < 1280; s++) odom_update(&p, 1, 3, &rb);
    CHECK(NEAR(p.x, 0.5, 1e-6) && NEAR(p.y, 0.5, 1e-6) && NEAR(p.theta, PI / 2, 1e-9));

    /* 7. heading wrap: 3.0 + 1.256637 = 4.256637 -> -2.026548 */
    p = (Pose){ 0, 0, 3.0 };
    odom_update(&p, -1024, 1024, &rb);
    CHECK(NEAR(p.theta, -2.026548, 1e-6));

    /* 8. wrap_angle range is (-pi, pi] */
    CHECK(NEAR(wrap_angle(PI), PI, 1e-12) && NEAR(wrap_angle(-PI), PI, 1e-12));
    CHECK(NEAR(wrap_angle(1.5 * PI), -0.5 * PI, 1e-12) && NEAR(wrap_angle(-7.0), -7.0 + 2 * PI, 1e-12));
    CHECK(NEAR(wrap_angle(0.25), 0.25, 1e-15));

    /* 9. encoder wrap feeding odometry: 65000 -> 1512 is +2048 ticks on both wheels */
    p = (Pose){ 0, 0, 0 };
    odom_update(&p, enc_delta(65000, 1512), enc_delta(65000, 1512), &rb);
    CHECK(NEAR(p.x, 0.628319, 1e-6) && NEAR(p.y, 0.0, 1e-12));

    /* 10. square path: 4 waypoints, v = 0.5 m/s, k = 3, dt = 10 ms, tol = 0.1 m -> 16.09 s */
    const Point sq[] = { {2, 0}, {2, 2}, {0, 2}, {0, 0} };
    p = (Pose){ 0, 0, 0 }; t = -1;
    CHECK(follow_waypoints(&p, sq, 4, &rb, 0.5, 3.0, 0.01, 0.1, 60.0, &t));
    CHECK(NEAR(t, 16.09, 0.05) && hypot(p.x, p.y) <= 0.1);

    /* 11. zero waypoints: done immediately, pose untouched */
    p = (Pose){ 1, 1, 0.5 }; t = -1;
    CHECK(follow_waypoints(&p, NULL, 0, &rb, 0.5, 3.0, 0.01, 0.1, 60.0, &t));
    CHECK(t == 0.0 && p.x == 1.0 && p.y == 1.0 && p.theta == 0.5);

    /* 12. already inside reach_tol of the only waypoint */
    const Point here[] = { {0.05, 0} };
    p = (Pose){ 0, 0, 0 }; t = -1;
    CHECK(follow_waypoints(&p, here, 1, &rb, 0.5, 3.0, 0.01, 0.1, 60.0, &t));
    CHECK(t == 0.0 && p.x == 0.0);

    /* 13. timeout: the square needs ~16 s, t_max = 5 s -> false, t_done untouched */
    p = (Pose){ 0, 0, 0 }; t = -1;
    CHECK(!follow_waypoints(&p, sq, 4, &rb, 0.5, 3.0, 0.01, 0.1, 5.0, &t));
    CHECK(t == -1);

    /* 14. heading error crosses +-pi: facing 170 deg, target at bearing -168.7 deg.
     *     Shortest turn is +21.3 deg (left). Turning right the long way takes 5.00 s. */
    const Point behind[] = { {-1.0, -0.2} };
    const double th0 = 170.0 * PI / 180.0;
    p = (Pose){ 0, 0, th0 }; t = -1;
    CHECK(!follow_waypoints(&p, behind, 1, &rb, 0.5, 3.0, 0.01, 0.1, 0.01, &t));  /* one step */
    CHECK(p.theta > th0);                                                       /* turned left */
    p = (Pose){ 0, 0, th0 }; t = -1;
    CHECK(follow_waypoints(&p, behind, 1, &rb, 0.5, 3.0, 0.01, 0.1, 60.0, &t));
    CHECK(NEAR(t, 1.86, 0.05));

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
