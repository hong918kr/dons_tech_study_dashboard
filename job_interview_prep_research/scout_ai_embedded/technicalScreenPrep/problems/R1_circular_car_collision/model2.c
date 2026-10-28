/*
 * R1 model answer - Solution 2 (Don's first idea: describe each path by its circle)
 *
 * Same inputs as Solution 1 (x, y, heading, speed, omega). With constant speed and
 * omega the car stays on ONE circle, so we convert the parameters to that circle once:
 *   r       radius of the path            r = |speed / omega|        (meters)
 *   cx, cy  center of the path            to the LEFT of the car when omega > 0,
 *                                         to the RIGHT when omega < 0
 *   phase   angle of the car seen from the center (radians, 0 = +x, CCW positive)
 *   phase0  that angle at t = 0
 * The car goes around the center at the same rate its heading turns, so
 *   phase(t) = phase0 + omega * t
 *   x(t) = cx + r * cos(phase(t)),   y(t) = cy + r * sin(phase(t))
 * Position at ANY time comes straight from the formula - no stepping, no drift.
 * Limitation: omega == 0 (straight line) has no circle.
 *
 * Build & run:  ./run.sh R1 model2
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define PI           3.14159265358979323846
#define DEG(d)       ((d) * PI / 180.0)
#define COLLIDE_DIST 1.0                  /* m */
#define DT           0.01                 /* s: how often we check the distance */
#define T_MAX        60.0                 /* s */

typedef struct {
    double cx, cy;    /* center of the circle (m) */
    double r;         /* radius (m) */
    double omega;     /* rad/s, + = counter-clockwise around the center */
    double phase0;    /* angle from center to car at t = 0 (rad) */
} CircleCar;

/* Convert the interviewer's parameters to a circle. Returns false for a straight line. */
bool circle_from_params(double x, double y, double heading, double speed, double omega,
                        CircleCar *out)
{
    if (fabs(omega) < 1e-9) return false;     /* omega == 0: no circle */
    double rs = speed / omega;                /* signed radius: + center on the left */
    out->cx = x - rs * sin(heading);          /* left of heading = heading + 90 deg */
    out->cy = y + rs * cos(heading);
    out->r = fabs(rs);
    out->omega = omega;
    out->phase0 = atan2(y - out->cy, x - out->cx);
    return true;
}

/* Position at time t, straight from the formula. */
void position_at(const CircleCar *c, double t, double *x, double *y)
{
    double phase = c->phase0 + c->omega * t;
    *x = c->cx + c->r * cos(phase);
    *y = c->cy + c->r * sin(phase);
}

int main(void)
{
    CircleCar a, b;
    /*                    x    y    heading  speed  omega */
    circle_from_params(4.0, 0.0, DEG(90), 2.0, +0.5, &a);
    circle_from_params(6.0, 3.0, DEG(0),  1.2, -0.4, &b);
    printf("A: center (%.2f, %.2f) r = %.2f   B: center (%.2f, %.2f) r = %.2f\n",
           a.cx, a.cy, a.r, b.cx, b.cy, b.r);

    for (long k = 0; ; k++) {
        double t = k * DT;
        if (t > T_MAX) {
            printf("no collision within %.0f s\n", T_MAX);
            return 0;
        }
        double ax, ay, bx, by;
        position_at(&a, t, &ax, &ay);         /* jump straight to time t */
        position_at(&b, t, &bx, &by);
        double dx = ax - bx, dy = ay - by;
        if (dx * dx + dy * dy <= COLLIDE_DIST * COLLIDE_DIST) {
            printf("collision at t = %.2f s (step %ld)\n", t, k);
            printf("  A at (%.3f, %.3f)   B at (%.3f, %.3f)   distance %.3f m\n",
                   ax, ay, bx, by, hypot(dx, dy));
            return 0;
        }
    }
}
