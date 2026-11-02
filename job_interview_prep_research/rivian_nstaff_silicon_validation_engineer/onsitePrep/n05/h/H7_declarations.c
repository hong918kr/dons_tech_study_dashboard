/* H7 · reading declarations (right-left rule). Types are PROVEN with
 * __builtin_types_compatible_p (gcc/clang). -DSHOW_ERRORS shows the
 * compile errors for writing through the const parts (run.sh records them). */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SAME(a, b) __builtin_types_compatible_p(a, b)

typedef void (*isr_t)(void);              /* pointer to function () -> void */
typedef int  (*cmp_fn)(const void *, const void *);

static void default_handler(void) {}
static void uart_isr(void) {}
static isr_t vector_table[4] = {default_handler, uart_isr, default_handler, default_handler};

/* "signal(): function taking (int, handler) returning handler" — raw vs typedef */
typedef void (*sighandler_t)(int);
void (*my_signal(int sig, void (*h)(int)))(int);
sighandler_t my_signal2(int sig, sighandler_t h);
static void h0(int s) { (void)s; }
void (*my_signal(int sig, void (*h)(int)))(int) { (void)sig; (void)h; return h0; }
sighandler_t my_signal2(int sig, sighandler_t h) { return my_signal(sig, h); }

int main(void) {
    int x = 1, y = 2;
    int row[4] = {0, 1, 2, 3};
    int grid[3][4] = {{0}};

    const int *p1 = &x;          /* pointer to const int   : *p1 read-only, p1 movable */
    int const *p2 = &x;          /* same type as p1                                    */
    int *const p3 = &x;          /* const pointer to int   : *p3 writable, p3 fixed    */
    const int *const p4 = &x;    /* both fixed                                         */
    int *ap[4];                  /* array of 4 pointers to int                         */
    int (*pa)[4] = &row;         /* pointer to array of 4 int                          */
    int (*g)[4] = grid;          /* grid decays to pointer to its first row            */

    _Static_assert(SAME(__typeof__(p1), __typeof__(p2)), "const int * == int const *");
    _Static_assert(!SAME(__typeof__(p1), __typeof__(p3)), "differs");
    _Static_assert(SAME(__typeof__(ap), int *[4]), "array of pointers");
    _Static_assert(SAME(__typeof__(pa), int (*)[4]), "pointer to array");
    _Static_assert(SAME(__typeof__(&grid[0]), int (*)[4]), "row pointer");
    _Static_assert(SAME(__typeof__(vector_table[1]), void (*)(void)), "isr_t");
    _Static_assert(SAME(__typeof__(&my_signal), __typeof__(&my_signal2)), "same fn type");

    p1 = &y;                     /* OK: the pointer itself is not const */
    *p3 = 5;                     /* OK: the pointee is not const        */
    printf("*p1=%d *p3=%d x=%d *p4=%d\n", *p1, *p3, x, *p4);
    assert(*p1 == 2 && x == 5 && *p4 == 5);

    ap[0] = &x;
    printf("sizeof ap=%zu  sizeof pa=%zu  sizeof *pa=%zu\n", sizeof ap, sizeof pa, sizeof *pa);
    assert(sizeof ap == 4 * sizeof(int *) && sizeof pa == sizeof(void *) && sizeof *pa == 16);

    /* pointer arithmetic scales by the POINTEE size */
    ptrdiff_t step_pa = (char *)(pa + 1) - (char *)pa;
    ptrdiff_t step_g  = (char *)(g + 1)  - (char *)g;
    printf("pa+1 moves %td bytes, g+1 moves %td bytes, (*pa)[2]=%d\n", step_pa, step_g, (*pa)[2]);
    assert(step_pa == 16 && step_g == 16 && (*pa)[2] == 2);

    vector_table[1]();           /* call through a function-pointer table */
    assert(vector_table[1] == uart_isr && my_signal2(0, h0) == h0);

#ifdef SHOW_ERRORS
    *p1 = 3;                     /* error: read-only variable */
    p3 = &y;                     /* error: cannot assign to const pointer */
#endif
    puts("H7 PASS");
    return 0;
}
