/*
 * R1 model answer - Solution 1 (the interviewer's approach)
 *
 * Each car is described by exactly the parameters the interviewer gave:
 *   x, y      position on the plane (meters)
 *   heading   the direction the car is pointing (radians)
 *             0 = pointing along +x (right), PI/2 = +y (up), PI = -x, -PI/2 = -y
 *   speed     how far the car moves per second along its heading (m/s)
 *   omega     rotational speed: how fast the heading turns (rad/s)
 *             omega > 0 turns left (counter-clockwise), omega < 0 turns right
 *
 * We do not know the "next point" in advance - we COMPUTE it from these parameters.
 * Time advances in small steps of DT seconds. In one step:
 *   the car moves speed*DT meters along its current heading, then
 *   the heading turns by omega*DT radians.
 * Repeating this traces a circle of radius speed/omega without ever storing a radius.
 *
 * Build & run:  ./run.sh R1 model1
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define PI           3.14159265358979323846
#define DEG(d)       ((d) * PI / 180.0)   /* degrees -> radians (cos/sin take radians) */
#define COLLIDE_DIST 1.0                  /* m: centers this close = collision */
#define DT           0.01                 /* s: one simulation step (100 steps per second) */
#define T_MAX        60.0                 /* s: stop if no collision by then */

typedef struct {
    double x, y;      /* m */
    double heading;   /* rad, 0 = +x, counter-clockwise positive */
    double speed;     /* m/s along the heading */
    double omega;     /* rad/s, + = turn left, - = turn right */
} Car;

Car init_car(double x, double y, double heading, double speed, double omega)
{
    Car c = { x, y, heading, speed, omega };
    return c;
}

/* Parameters -> next position. Called once per time step. */
void update_car(Car *c, double dt)
{
    double dist = c->speed * dt;              /* meters travelled in this step */
    c->x += dist * cos(c->heading);           /* x part of the move */
    c->y += dist * sin(c->heading);           /* y part of the move */
    c->heading += c->omega * dt;              /* turn for the next step */
}

/* Collision = distance between centers <= COLLIDE_DIST (compare squares, no sqrt). */
bool did_collide(const Car *a, const Car *b)
{
    double dx = a->x - b->x;
    double dy = a->y - b->y;
    return dx * dx + dy * dy <= COLLIDE_DIST * COLLIDE_DIST;
}

int main(void)
{
    /*                x    y    heading   speed  omega            */
    Car a = init_car(4.0, 0.0, DEG(90),  2.0,  +0.5);   /* points up, turns left  */
    Car b = init_car(6.0, 3.0, DEG(0),   1.2,  -0.4);   /* points right, turns right */

    for (long k = 0; ; k++) {                 /* k = step number */
        double t = k * DT;                    /* current time (s); k*DT avoids += drift */
        if (t > T_MAX) {
            printf("no collision within %.0f s\n", T_MAX);
            return 0;
        }
        if (did_collide(&a, &b)) {
            printf("collision at t = %.2f s (step %ld)\n", t, k);
            printf("  A at (%.3f, %.3f)   B at (%.3f, %.3f)   distance %.3f m\n",
                   a.x, a.y, b.x, b.y, hypot(a.x - b.x, a.y - b.y));
            return 0;
        }
        update_car(&a, DT);                   /* both cars move to their next point */
        update_car(&b, DT);
    }
}
