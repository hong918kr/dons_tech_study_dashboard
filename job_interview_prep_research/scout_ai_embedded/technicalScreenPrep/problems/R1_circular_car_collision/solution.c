/*
 * R1  Circular Car Collision  (actual Scout AI screen question, 2026-10-26)  -- Reference solution
 *
 * Two cars drive on a 2D plane. The interviewer's state for each car:
 *     x, y      position (m)
 *     heading   direction of travel (rad, 0 = +x axis, counter-clockwise positive)
 *     speed     m/s along the heading
 *     omega     rotational speed (rad/s, > 0 turns left / CCW, < 0 turns right / CW)
 * From these parameters you can always compute the car's NEXT position.
 * Speed and omega are constant, so each car drives on a circle (a line if omega == 0).
 * The cars collide when their centers are within COLLIDE_DIST meters.
 * Report the timestamp (seconds) of the first collision, or -1 if none before t_max.
 *
 * Part A - Solution 1 (step the state):  update_car, did_collide, sim_collision_time
 * Part B - Solution 1 refined:           update_car_exact, find_collision_time
 * Part C - Solution 2 (circle radius):   circle_from_car, circle_pos, circle_collision_time
 *
 * Build & run:  ./run.sh R1 sol
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)
#define NEAR(a, b, eps) (fabs((a) - (b)) <= (eps))

static const double PI = 3.14159265358979323846;
#define COLLIDE_DIST 1.0                       /* meters between centers = contact */

typedef struct {
    double x, y;      /* position (m) */
    double heading;   /* rad */
    double speed;     /* m/s */
    double omega;     /* rad/s */
} Car;

Car init_car(double x, double y, double heading, double speed, double omega)
{
    Car c = { x, y, heading, speed, omega };
    return c;
}

/* ---------------- Part A: Solution 1 - parameters -> next position ---------------- */

/* One time step: move along the current heading, then turn. */
void update_car(Car *c, double dt)
{
    c->x += c->speed * cos(c->heading) * dt;
    c->y += c->speed * sin(c->heading) * dt;
    c->heading += c->omega * dt;
}

bool did_collide(const Car *a, const Car *b)
{
    double dx = a->x - b->x, dy = a->y - b->y;
    return dx * dx + dy * dy <= COLLIDE_DIST * COLLIDE_DIST;   /* no sqrt */
}

/* The interview loop: check, step both cars, advance time. */
double sim_collision_time(Car a, Car b, double dt, double t_max)
{
    for (long k = 0; k * dt <= t_max; k++) {   /* t = k*dt: no accumulated += error */
        if (did_collide(&a, &b)) return k * dt;
        update_car(&a, dt);
        update_car(&b, dt);
    }
    return -1.0;
}

/* ---------------- Part B: Solution 1 refined ---------------- */

/* Same parameters, but integrate along the arc: exact for any dt. */
void update_car_exact(Car *c, double dt)
{
    double th0 = c->heading;
    double th1 = th0 + c->omega * dt;
    if (fabs(c->omega) < 1e-9) {               /* straight line */
        c->x += c->speed * cos(th0) * dt;
        c->y += c->speed * sin(th0) * dt;
    } else {
        double r = c->speed / c->omega;        /* signed turn radius, derived */
        c->x += r * (sin(th1) - sin(th0));
        c->y += r * (cos(th0) - cos(th1));
    }
    c->heading = remainder(th1, 2.0 * PI);     /* keep in [-pi, pi] */
}

/* Coarse steps that cannot jump through a contact, then bisection in the hit step. */
double find_collision_time(Car a, Car b, double t_max, double tol)
{
    if (did_collide(&a, &b)) return 0.0;
    double vmax = fabs(a.speed) + fabs(b.speed);   /* bound on closing speed */
    if (vmax == 0.0) return -1.0;
    double dt = 0.5 * COLLIDE_DIST / vmax;

    double t = 0.0;
    while (t < t_max) {
        double step = (t + dt > t_max) ? t_max - t : dt;
        Car a1 = a, b1 = b;
        update_car_exact(&a1, step);
        update_car_exact(&b1, step);
        if (did_collide(&a1, &b1)) {
            double lo = 0.0, hi = step;        /* no contact at lo, contact at hi */
            while (hi - lo > tol) {
                double mid = 0.5 * (lo + hi);
                Car a2 = a, b2 = b;
                update_car_exact(&a2, mid);
                update_car_exact(&b2, mid);
                if (did_collide(&a2, &b2)) hi = mid; else lo = mid;
            }
            return t + hi;
        }
        a = a1; b = b1; t += step;
    }
    return -1.0;
}

/* ---------------- Part C: Solution 2 - describe the path by its circle ---------------- */

typedef struct {
    double cx, cy;    /* circle center (m) */
    double r;         /* path radius (m), > 0 */
    double omega;     /* angular speed around the center (rad/s), sign = direction */
    double phase0;    /* angle of the car seen from the center at t = 0 (rad) */
} CircleCar;

/* Convert the interviewer's state to a circle. False for a straight line (omega == 0). */
bool circle_from_car(const Car *c, CircleCar *out)
{
    if (fabs(c->omega) < 1e-9) return false;
    double rs = c->speed / c->omega;           /* signed: + center on the left */
    out->cx = c->x - rs * sin(c->heading);
    out->cy = c->y + rs * cos(c->heading);
    out->r = fabs(rs);
    out->omega = c->omega;
    out->phase0 = atan2(c->y - out->cy, c->x - out->cx);
    return true;
}

/* Position at any time t directly - no stepping, no accumulated error. */
void circle_pos(const CircleCar *k, double t, double *x, double *y)
{
    double ph = k->phase0 + k->omega * t;
    *x = k->cx + k->r * cos(ph);
    *y = k->cy + k->r * sin(ph);
}

static double circle_dist2(const CircleCar *a, const CircleCar *b, double t)
{
    double ax, ay, bx, by;
    circle_pos(a, t, &ax, &ay);
    circle_pos(b, t, &bx, &by);
    return (ax - bx) * (ax - bx) + (ay - by) * (ay - by);
}

double circle_collision_time(const CircleCar *a, const CircleCar *b, double t_max, double tol)
{
    const double d2 = COLLIDE_DIST * COLLIDE_DIST;
    if (circle_dist2(a, b, 0.0) <= d2) return 0.0;
    double vmax = fabs(a->omega) * a->r + fabs(b->omega) * b->r;   /* linear speeds */
    double dt = 0.5 * COLLIDE_DIST / vmax;
    for (double t = 0.0; t < t_max; t += dt) {
        double t1 = (t + dt > t_max) ? t_max : t + dt;
        if (circle_dist2(a, b, t1) <= d2) {
            double lo = t, hi = t1;
            while (hi - lo > tol) {
                double mid = 0.5 * (lo + hi);
                if (circle_dist2(a, b, mid) <= d2) hi = mid; else lo = mid;
            }
            return hi;
        }
    }
    return -1.0;
}

int main(void)
{
    /* main example (interviewer-style parameters) */
    Car a = init_car(4, 0, PI / 2, 2.0, 0.5);    /* CCW, turn radius 4, center (0,0) */
    Car b = init_car(6, 3, 0, 1.2, -0.4);        /* CW,  turn radius 3, center (6,0) */

    /* ---- Part A: Solution 1 ---- */
    /* 1. one step from the parameters */
    Car s = a;
    update_car(&s, 0.5);
    CHECK(NEAR(s.x, 4.0, 1e-12) && NEAR(s.y, 1.0, 1e-12) && NEAR(s.heading, PI / 2 + 0.25, 1e-12));

    /* 2. contact rule: exactly COLLIDE_DIST counts, a hair more does not */
    Car p = init_car(0, 0, 0, 0, 0), q = init_car(1.0, 0, 0, 0, 0), r = init_car(1.0001, 0, 0, 0, 0);
    CHECK(did_collide(&p, &q));
    CHECK(!did_collide(&p, &r));

    /* 3. simulation loop on the main example */
    double t_s = sim_collision_time(a, b, 0.01, 60);
    printf("Solution 1 (step dt=0.01):        t = %.2f s\n", t_s);
    CHECK(NEAR(t_s, 12.82, 1e-9));
    CHECK(NEAR(sim_collision_time(a, b, 0.5, 60), 12.50, 1e-9));

    /* 4. never meet -> -1 (parallel lanes 5 m apart), already touching -> 0 */
    Car l1 = init_car(0, 0, 0, 1, 0), l2 = init_car(0, 5, 0, 1, 0);
    CHECK(sim_collision_time(l1, l2, 0.01, 60) == -1.0);
    Car o1 = init_car(0, 0, 0, 1, 0), o2 = init_car(0.8, 0, 0, 1, 0);
    CHECK(sim_collision_time(o1, o2, 0.01, 60) == 0.0);

    /* ---- Part B: Solution 1 refined ---- */
    /* 5. exact step: a quarter lap ends at (0,4) heading pi */
    Car e = a;
    update_car_exact(&e, PI);                   /* quarter of the 4*pi s period */
    CHECK(NEAR(e.x, 0.0, 1e-9) && NEAR(e.y, 4.0, 1e-9) && NEAR(fabs(e.heading), PI, 1e-9));

    /* 6. exact timestamp */
    double t_b = find_collision_time(a, b, 60, 1e-9);
    printf("Solution 1 refined (exact+bisect): t = %.6f s\n", t_b);
    CHECK(NEAR(t_b, 12.822241, 1e-6));

    /* 7. straight head-on: gap 10 - 2t = 1 -> 4.5 s */
    Car h1 = init_car(0, 0, 0, 1, 0), h2 = init_car(10, 0, PI, 1, 0);
    CHECK(NEAR(find_collision_time(h1, h2, 60, 1e-9), 4.5, 1e-6));

    /* 8. tunneling: fast cars, dt = 1 s loop jumps through each other */
    Car f1 = init_car(0, 0, 0, 10, 0), f2 = init_car(10, 0.0, PI, 10, 0);
    CHECK(sim_collision_time(f1, f2, 1.0, 5) == -1.0);
    CHECK(NEAR(find_collision_time(f1, f2, 5, 1e-9), 0.45, 1e-6));

    /* 9. t_max cut-off */
    CHECK(find_collision_time(a, b, 10, 1e-9) == -1.0);

    /* ---- Part C: Solution 2 ---- */
    /* 10. parameters -> circle */
    CircleCar ca, cb;
    CHECK(circle_from_car(&a, &ca) && circle_from_car(&b, &cb));
    CHECK(NEAR(ca.cx, 0, 1e-12) && NEAR(ca.cy, 0, 1e-12) && NEAR(ca.r, 4, 1e-12));
    CHECK(NEAR(cb.cx, 6, 1e-12) && NEAR(cb.cy, 0, 1e-12) && NEAR(cb.r, 3, 1e-12));
    CHECK(!circle_from_car(&h1, &ca));           /* omega == 0 has no circle */
    circle_from_car(&a, &ca);

    /* 11. same position as Solution 1 refined at any t */
    double cx, cy;
    Car a7 = a;
    update_car_exact(&a7, 7.3);
    circle_pos(&ca, 7.3, &cx, &cy);
    CHECK(NEAR(cx, a7.x, 1e-9) && NEAR(cy, a7.y, 1e-9));

    /* 12. same timestamp */
    double t_c = circle_collision_time(&ca, &cb, 60, 1e-9);
    printf("Solution 2 (circle, closed form):  t = %.6f s\n", t_c);
    CHECK(NEAR(t_c, 12.822241, 1e-6));

    if (g_fail == 0) printf("ALL PASS\n"); else printf("%d FAILED\n", g_fail);
    return g_fail != 0;
}
