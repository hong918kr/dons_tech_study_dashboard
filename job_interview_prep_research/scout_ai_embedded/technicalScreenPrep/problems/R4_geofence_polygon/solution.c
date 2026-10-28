/*
 * R4  Geofence Monitor  (point-in-polygon + segment crossing + time of breach)
 *
 * A UGV receives GPS fixes already converted to local ENU meters (x = east, y = north).
 * The operating area is a simple polygon (may be CONCAVE), given as a vertex table.
 *
 * Part A - geometry primitives
 *   cross(o, a, b)          z of (a - o) x (b - o): > 0 if b is LEFT of o->a, < 0 right, 0 collinear.
 *   seg_intersect(p1,p2,q1,q2,&t)
 *                           true if segment p1-p2 touches/crosses segment q1-q2 at ONE point;
 *                           *t = parameter along p1->p2 (0 = p1, 1 = p2) of that point.
 *                           Endpoints touching count. PARALLEL or COLLINEAR segments return false
 *                           (no single crossing point; driving along the fence line = "on fence").
 *   point_in_polygon(p,poly,n)
 *                           even-odd ray casting (+x ray). Works for concave polygons.
 *                           A point exactly ON an edge or vertex counts as INSIDE. n < 3 -> false.
 * Part B
 *   dist_to_polygon(p,poly,n)
 *                           minimum distance from p to any edge (clamped projection).
 *                           n < 3 or poly == NULL -> -1.0 (invalid).
 * Part C - streaming monitor
 *   fence_init(f, poly, n, warn_margin_m)   poly must stay alive (const table in flash).
 *   fence_update(f, pos, t_ms, &breach_t_ms) -> event, emitted ONLY on a state change:
 *       INSIDE : inside and dist >= warn_margin
 *       WARN   : inside and dist <  warn_margin; leave WARN (or BREACH) for INSIDE only when
 *                dist >= warn_margin + FENCE_HYST_M (hysteresis)
 *       BREACH : outside
 *   On INSIDE/WARN -> BREACH: *breach_t_ms = interpolated time of the FIRST fence crossing on the
 *       segment prev_fix -> pos:  t_prev + u * (t_now - t_prev)   (uint32 wrap-safe).
 *       First fix ever outside: *breach_t_ms = t_ms.
 *   Both fixes inside but the segment crosses the fence at >= 2 distinct points (jump across a
 *       notch of a concave fence) -> FE_TRANSIT, *breach_t_ms = first crossing time, f->transits++.
 *   Invalid fence (n < 3, NULL, margin < 0) -> every update returns FE_FAULT (fail-safe).
 *   breach_t_ms may be NULL.  No malloc.
 *
 * Reference solution.  Build & run:  ./run.sh R4 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <math.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)
#define NEAR(a, b) (fabs((a) - (b)) < 1e-6)

typedef struct { double x, y; } Vec2;            /* local ENU meters */

#define GEO_EPS      1e-9                        /* "zero" for cross products (m^2) */
#define FENCE_HYST_M 1.0                         /* extra margin needed to leave WARN */

typedef enum { FENCE_INSIDE, FENCE_WARN, FENCE_BREACH } FenceState;
typedef enum { FE_NONE, FE_INSIDE, FE_WARN, FE_BREACH, FE_TRANSIT, FE_FAULT } FenceEvent;

typedef struct {
    const Vec2 *poly;
    size_t      n;
    double      warn_m;
    bool        valid;
    FenceState  state;
    bool        has_prev;
    Vec2        prev;
    uint32_t    prev_t;
    uint32_t    transits;                        /* how many notch transits were seen */
} Fence;

/* ---------- Part A ---------- */

double cross(Vec2 o, Vec2 a, Vec2 b)
{
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

static int sgn(double v) { return (v > GEO_EPS) - (v < -GEO_EPS); }

bool seg_intersect(Vec2 p1, Vec2 p2, Vec2 q1, Vec2 q2, double *t)
{
    double d1 = cross(q1, q2, p1), d2 = cross(q1, q2, p2);   /* side of p1, p2 vs line q */
    double d3 = cross(p1, p2, q1), d4 = cross(p1, p2, q2);   /* side of q1, q2 vs line p */
    int s1 = sgn(d1), s2 = sgn(d2), s3 = sgn(d3), s4 = sgn(d4);

    if (s1 == 0 && s2 == 0) return false;        /* collinear: no single crossing point */
    if (s1 * s2 > 0 || s3 * s4 > 0) return false;  /* both on the same side (incl. parallel) */
    if (t) *t = d1 / (d1 - d2);                  /* where the signed distance hits zero */
    return true;
}

static bool on_segment(Vec2 p, Vec2 a, Vec2 b)
{
    return sgn(cross(a, b, p)) == 0 &&
           fmin(a.x, b.x) - GEO_EPS <= p.x && p.x <= fmax(a.x, b.x) + GEO_EPS &&
           fmin(a.y, b.y) - GEO_EPS <= p.y && p.y <= fmax(a.y, b.y) + GEO_EPS;
}

bool point_in_polygon(Vec2 p, const Vec2 *poly, size_t n)
{
    if (poly == NULL || n < 3) return false;
    bool inside = false;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        Vec2 a = poly[j], b = poly[i];
        if (on_segment(p, a, b)) return true;    /* boundary counts as inside */
        /* half-open rule: an edge counts only if p.y is in [min y, max y).
         * A vertex exactly on the ray is then counted once, horizontal edges never. */
        if ((a.y > p.y) != (b.y > p.y)) {
            double x_hit = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (x_hit > p.x) inside = !inside;
        }
    }
    return inside;
}

/* ---------- Part B ---------- */

static double dist_point_seg(Vec2 p, Vec2 a, Vec2 b)
{
    double dx = b.x - a.x, dy = b.y - a.y;
    double len2 = dx * dx + dy * dy;
    double u = 0.0;
    if (len2 > 0.0) {
        u = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
        u = u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);   /* clamp to the segment */
    }
    double ex = a.x + u * dx - p.x, ey = a.y + u * dy - p.y;
    return sqrt(ex * ex + ey * ey);
}

double dist_to_polygon(Vec2 p, const Vec2 *poly, size_t n)
{
    if (poly == NULL || n < 3) return -1.0;
    double best = INFINITY;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        double d = dist_point_seg(p, poly[j], poly[i]);
        if (d < best) best = d;
    }
    return best;
}

/* ---------- Part C ---------- */

void fence_init(Fence *f, const Vec2 *poly, size_t n, double warn_margin_m)
{
    f->poly = poly;
    f->n = n;
    f->warn_m = warn_margin_m;
    f->valid = poly != NULL && n >= 3 && warn_margin_m >= 0.0;
    f->state = FENCE_INSIDE;                     /* armed inside the area */
    f->has_prev = false;
    f->prev = (Vec2){ 0.0, 0.0 };
    f->prev_t = 0;
    f->transits = 0;
}

/* Distinct crossing parameters of segment a->b with the fence: first (min) and last (max). */
static size_t fence_crossings(const Fence *f, Vec2 a, Vec2 b, double *u_first, double *u_last)
{
    size_t hits = 0;
    *u_first = 1.0; *u_last = 0.0;
    for (size_t i = 0, j = f->n - 1; i < f->n; j = i++) {
        double u;
        if (seg_intersect(a, b, f->poly[j], f->poly[i], &u)) {
            hits++;
            if (u < *u_first) *u_first = u;
            if (u > *u_last)  *u_last = u;
        }
    }
    return hits;
}

static uint32_t lerp_time(uint32_t t0, uint32_t t1, double u)
{
    uint32_t dt = t1 - t0;                       /* wrap-safe elapsed ticks */
    return t0 + (uint32_t)(u * (double)dt + 0.5);  /* uint32 add wraps naturally */
}

FenceEvent fence_update(Fence *f, Vec2 pos, uint32_t t_ms, uint32_t *breach_t_ms)
{
    if (!f->valid) return FE_FAULT;

    bool in = point_in_polygon(pos, f->poly, f->n);
    FenceState old = f->state, next;
    if (!in) {
        next = FENCE_BREACH;
    } else {
        double d = dist_to_polygon(pos, f->poly, f->n);
        if (old == FENCE_INSIDE) next = (d < f->warn_m) ? FENCE_WARN : FENCE_INSIDE;
        else                     next = (d >= f->warn_m + FENCE_HYST_M) ? FENCE_INSIDE : FENCE_WARN;
    }

    FenceEvent ev = FE_NONE;
    if (next != old) ev = (next == FENCE_INSIDE) ? FE_INSIDE : (next == FENCE_WARN) ? FE_WARN : FE_BREACH;

    if (next == FENCE_BREACH && old != FENCE_BREACH) {
        uint32_t tb = t_ms;                      /* no previous fix: breach "now" */
        double u0, u1;
        if (f->has_prev && fence_crossings(f, f->prev, pos, &u0, &u1) > 0)
            tb = lerp_time(f->prev_t, t_ms, u0);
        if (breach_t_ms) *breach_t_ms = tb;
    } else if (in && old != FENCE_BREACH && f->has_prev) {
        double u0, u1;                           /* inside -> inside: did we cut a notch? */
        if (fence_crossings(f, f->prev, pos, &u0, &u1) >= 2 && u1 - u0 > 1e-6) {
            f->transits++;
            if (breach_t_ms) *breach_t_ms = lerp_time(f->prev_t, t_ms, u0);
            ev = FE_TRANSIT;                     /* overrides INSIDE/WARN change */
        }
    }

    f->state = next;
    f->prev = pos;
    f->prev_t = t_ms;
    f->has_prev = true;
    return ev;
}

/* ---------- tests ---------- */

/* U-shaped area, 60 m x 50 m, notch x in (20,40), y in (20,50). Counter-clockwise. */
static const Vec2 U[] = { {0,0}, {60,0}, {60,50}, {40,50}, {40,20}, {20,20}, {20,50}, {0,50} };
#define UN (sizeof U / sizeof U[0])

int main(void)
{
    double t;
    uint32_t bt;

    /* 1. primitives, normal cases */
    CHECK(NEAR(cross((Vec2){0,0}, (Vec2){10,0}, (Vec2){0,10}), 100.0));   /* left  */
    CHECK(NEAR(cross((Vec2){0,0}, (Vec2){10,0}, (Vec2){0,-10}), -100.0)); /* right */
    t = -1;
    CHECK(seg_intersect((Vec2){58,30}, (Vec2){64,38}, (Vec2){60,0}, (Vec2){60,50}, &t) && NEAR(t, 1.0 / 3));
    CHECK(!seg_intersect((Vec2){50,10}, (Vec2){56,20}, (Vec2){60,0}, (Vec2){60,50}, &t));
    CHECK(point_in_polygon((Vec2){10,10}, U, UN));       /* left arm / base  */
    CHECK(point_in_polygon((Vec2){50,40}, U, UN));       /* right arm        */
    CHECK(!point_in_polygon((Vec2){30,35}, U, UN));      /* inside the notch */
    CHECK(!point_in_polygon((Vec2){70,10}, U, UN));
    CHECK(NEAR(dist_to_polygon((Vec2){56,20}, U, UN), 4.0));
    CHECK(NEAR(dist_to_polygon((Vec2){30,35}, U, UN), 10.0));  /* outside: still a distance */
    CHECK(NEAR(dist_to_polygon((Vec2){10,10}, U, UN), 10.0));

    /* 2. normal track: INSIDE -> WARN -> BREACH (interpolated) -> WARN -> INSIDE */
    Fence f = {0};
    fence_init(&f, U, UN, 5.0);
    const struct { Vec2 p; uint32_t t; FenceEvent ev; } trk[] = {
        { {10,10},   1000, FE_NONE   },
        { {30,8},    2000, FE_NONE   },
        { {50,10},   3000, FE_NONE   },
        { {56,20},   4000, FE_WARN   },   /* 4 m from east edge   */
        { {58,30},   5000, FE_NONE   },   /* still WARN (2 m)     */
        { {64,38},   6000, FE_BREACH },   /* crossed x = 60       */
        { {62,40},   7000, FE_NONE   },   /* still outside        */
        { {55,40},   8000, FE_WARN   },   /* back in, 5 m < 6 m   */
        { {54.5,35}, 9000, FE_NONE   },   /* 5.5 m: hysteresis holds WARN */
        { {50,30},  10000, FE_INSIDE },   /* 10 m >= 6 m          */
    };
    bt = 0;
    for (size_t i = 0; i < sizeof trk / sizeof trk[0]; i++) {
        FenceEvent ev = fence_update(&f, trk[i].p, trk[i].t, &bt);
        CHECK(ev == trk[i].ev);
    }
    CHECK(bt == 5333);                    /* 5000 + (1/3) * 1000 */
    CHECK(f.state == FENCE_INSIDE && f.transits == 0);

    /* 3. ray through a vertex: the classic double-count bug */
    const Vec2 dia[] = { {0,-10}, {10,0}, {0,10}, {-10,0} };
    CHECK(point_in_polygon((Vec2){0,0}, dia, 4));         /* ray hits vertex (10,0) */
    CHECK(!point_in_polygon((Vec2){-15,0}, dia, 4));      /* ray hits (-10,0) and (10,0) */
    CHECK(point_in_polygon((Vec2){10,20}, U, UN));        /* ray along y=20 hits 2 vertices */
    CHECK(!point_in_polygon((Vec2){30,50}, U, UN));       /* notch mouth, ray on top edge */

    /* 4. points exactly on the boundary count as inside */
    CHECK(point_in_polygon((Vec2){30,0}, U, UN));
    CHECK(point_in_polygon((Vec2){20,35}, U, UN));
    CHECK(point_in_polygon((Vec2){60,50}, U, UN));        /* a vertex */
    CHECK(NEAR(dist_to_polygon((Vec2){30,0}, U, UN), 0.0));

    /* 5. parallel / collinear / touching segments */
    CHECK(!seg_intersect((Vec2){0,0}, (Vec2){10,0}, (Vec2){5,0}, (Vec2){15,0}, &t));  /* collinear */
    CHECK(!seg_intersect((Vec2){0,0}, (Vec2){10,0}, (Vec2){0,1}, (Vec2){10,1}, &t));  /* parallel  */
    t = -1;
    CHECK(seg_intersect((Vec2){0,0}, (Vec2){10,0}, (Vec2){10,-5}, (Vec2){10,5}, &t) && NEAR(t, 1.0));

    /* 6. invalid fence (n < 3) is a fault; first fix outside breaches "now" */
    CHECK(!point_in_polygon((Vec2){0,0}, U, 2));
    CHECK(dist_to_polygon((Vec2){0,0}, U, 2) < 0.0);
    fence_init(&f, U, 2, 5.0);
    CHECK(fence_update(&f, (Vec2){10,10}, 0, &bt) == FE_FAULT);
    fence_init(&f, U, UN, 5.0);
    bt = 0;
    CHECK(fence_update(&f, (Vec2){-3,10}, 777, &bt) == FE_BREACH && bt == 777);

    /* 7. timer wrap between the two fixes: interpolation must wrap too */
    fence_init(&f, U, UN, 5.0);
    CHECK(fence_update(&f, (Vec2){52.5,25}, 4294966796u, &bt) == FE_NONE);   /* 2^32 - 500 */
    bt = 0;
    CHECK(fence_update(&f, (Vec2){62.5,25}, 500u, &bt) == FE_BREACH);
    CHECK(bt == 250u);                    /* (2^32 - 500) + 0.75 * 1000, wrapped */

    /* 8. GPS jump across the notch: both fixes inside, fence crossed twice */
    fence_init(&f, U, UN, 5.0);
    CHECK(fence_update(&f, (Vec2){10,40}, 0, &bt) == FE_NONE);
    bt = 0;
    CHECK(fence_update(&f, (Vec2){50,40}, 1000, &bt) == FE_TRANSIT);
    CHECK(bt == 250 && f.transits == 1 && f.state == FENCE_INSIDE);
    CHECK(fence_update(&f, (Vec2){50,30}, 2000, &bt) == FE_NONE);              /* no re-report */

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
