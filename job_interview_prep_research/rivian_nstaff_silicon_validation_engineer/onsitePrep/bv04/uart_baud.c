/*
 * BV04 UART baud-rate error calculator.
 *
 * Part 1: divisor error for two common baud generators
 *   (a) integer divisor, 16x oversampling (16550-style DLL/DLM):
 *         div = round(clk / (16 * baud)),  actual = clk / (16 * div)
 *   (b) Arm PL011 fractional divisor (TRM: BAUDDIV = UARTCLK / (16 * baud)
 *         = IBRD + FBRD/64, FBRD = integer(frac * 64 + 0.5))
 *   Verdict vs the rule of thumb "each side within +/-2.5%" (16x oversampled RX,
 *   ~5% total budget) — tighter for long/capacitive lines (~2% total, Maxim AN2141).
 *
 * Part 2: "UART prints garbage" — FW programs the divisor for the clock it THINKS the
 *   UART has, but the real clock is different (wrong crystal, PLL bypassed, wrong
 *   clock mux). Shows actual baud and error.
 *
 * Part 3: back-calculate baud from a bit width measured on a scope.
 *
 * Build: cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 \
 *          -Wall -Wextra -Werror -fsanitize=address,undefined uart_baud.c -o /tmp/uart_baud
 */
#include <math.h>
#include <stdio.h>

#define SIDE_TOL_PCT 2.5

static const char *verdict(double err_pct)
{
    double a = fabs(err_pct);
    if (a <= 1.0) return "OK";
    if (a <= SIDE_TOL_PCT) return "MARGINAL";
    return "FAIL (garbage)";
}

/* (a) integer divisor, 16x */
static double int_div_baud(double clk, double baud, unsigned *div_out)
{
    unsigned div = (unsigned)lround(clk / (16.0 * baud));
    if (div == 0) div = 1;
    *div_out = div;
    return clk / (16.0 * div);
}

/* (b) PL011: IBRD (16 bit) + FBRD (6 bit) */
static double pl011_baud(double clk, double baud, unsigned *ibrd, unsigned *fbrd)
{
    double bauddiv = clk / (16.0 * baud);
    unsigned i = (unsigned)floor(bauddiv);
    unsigned f = (unsigned)((bauddiv - i) * 64.0 + 0.5);
    if (f == 64) { i++; f = 0; }
    *ibrd = i;
    *fbrd = f;
    return clk / (16.0 * (i + f / 64.0));
}

int main(void)
{
    const double clks[] = { 1.8432e6, 19.2e6, 24e6, 48e6, 100e6 };
    const double bauds[] = { 9600, 115200, 921600, 3000000 };

    printf("Part 1: divisor error (16x oversampling)\n");
    printf("  UARTCLK     baud     | int div  actual      err%%    verdict        "
           "| PL011 IBRD+FBRD/64  actual      err%%    verdict\n");
    for (unsigned c = 0; c < sizeof clks / sizeof clks[0]; c++) {
        for (unsigned b = 0; b < sizeof bauds / sizeof bauds[0]; b++) {
            double clk = clks[c], baud = bauds[b];
            if (clk / (16.0 * baud) < 1.0) {
                printf("  %7.4f MHz %8.0f | clock too slow for 16x oversampling\n", clk / 1e6,
                       baud);
                continue;
            }
            unsigned div, ib, fb;
            double a1 = int_div_baud(clk, baud, &div);
            double a2 = pl011_baud(clk, baud, &ib, &fb);
            double e1 = (a1 - baud) / baud * 100.0, e2 = (a2 - baud) / baud * 100.0;
            printf("  %7.4f MHz %8.0f | %5u %10.1f %+8.3f  %-14s | %5u + %2u/64 %10.1f %+8.3f  %s\n",
                   clk / 1e6, baud, div, a1, e1, verdict(e1), ib, fb, a2, e2, verdict(e2));
        }
    }

    printf("\nPart 2: FW assumed one UART clock, hardware delivers another (115200 target)\n");
    struct { const char *why; double assumed, real; } sc[] = {
        { "correct clock",                          24e6,   24e6 },
        { "26 MHz crystal fitted instead of 24 MHz", 24e6,   26e6 },
        { "19.2 MHz osc option (BOM alternate)",     24e6,   19.2e6 },
        { "PLL bypassed: UART gets 24 MHz osc, FW assumed 48 MHz PLL out", 48e6, 24e6 },
        { "crystal +100 ppm (aging/temp)",           24e6,   24e6 * (1 + 100e-6) },
    };
    for (unsigned i = 0; i < sizeof sc / sizeof sc[0]; i++) {
        unsigned ib, fb;
        pl011_baud(sc[i].assumed, 115200, &ib, &fb);       /* divisor FW programs */
        double actual = sc[i].real / (16.0 * (ib + fb / 64.0));
        double e = (actual - 115200) / 115200 * 100.0;
        printf("  %-62s IBRD=%-3u FBRD=%-2u -> %9.1f baud  %+8.3f%%  %s\n", sc[i].why, ib, fb,
               actual, e, verdict(e));
    }

    printf("\nPart 3: baud from a bit width measured on the scope (start bit edge-to-edge)\n");
    const double bit_us[] = { 8.68, 8.70, 10.85, 17.36 };
    for (unsigned i = 0; i < sizeof bit_us / sizeof bit_us[0]; i++) {
        double baud = 1e6 / bit_us[i];
        double e = (baud - 115200) / 115200 * 100.0;
        printf("  bit = %6.2f us -> %9.1f baud  (vs 115200: %+7.2f%%  %s)\n", bit_us[i], baud,
               e, verdict(e));
    }
    return 0;
}
