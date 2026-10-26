/*
 * V5  Bare-metal Test Framework: registry, timeouts and coverage report
 *
 * The validation firmware has a static table of test cases. Each test is tagged with the
 * SoC block it exercises and the requirement it verifies. The same image runs on the FPGA
 * prototype and on silicon. A host-side Python script parses the UART log.
 *
 * Implement:
 *   tf_run(tests, n, cfg, res, res_cap)
 *     - select tests whose block equals cfg->block_filter (NULL = all blocks); others are
 *       not recorded at all.
 *     - a selected test is SKIP (rc 0, ms 0, fn not called) if
 *         (cfg->plat == PLAT_FPGA && flags & TF_SKIP_ON_FPGA) or
 *         (flags & TF_DESTRUCTIVE && !cfg->allow_destructive)
 *     - otherwise: start = tf_now_ms(); rc = fn(cfg->ctx); ms = tf_now_ms() - start (wrap-safe)
 *         ms > timeout_ms (timeout_ms != 0)  -> TIMEOUT (rc kept)
 *         else rc == 0 -> PASS, rc != 0 -> FAIL
 *     - results in table order; returns the number of results.
 *       If res_cap is smaller than the number of selected tests, run NOTHING and return -ENOSPC.
 *   tf_report(res, nres, reqs, nreq, buf, cap)
 *     Writes (bounded, always NUL-terminated if cap > 0) and returns the FULL length that
 *     would have been written (like snprintf), so the caller can retry with a bigger buffer:
 *       RESULT,<id>,<status>,<rc>,<ms>                 one line per result, in order
 *       SUMMARY,total=<n>,pass=<p>,fail=<f>,skip=<s>,timeout=<t>
 *       COVERAGE,<block>,<covered>/<total>,<pct>%      per block, first-appearance order in reqs
 *       COVERAGE,ALL,<covered>/<total>,<pct>%
 *       UNCOVERED,<req_id>                             in reqs order
 *     A requirement is covered if at least one result with the same req_id is PASS.
 *     pct = covered * 100 / total, rounded down (0 if total == 0). Every line ends with '\n'.
 *   No malloc.
 *
 * Build & run:  ./run.sh V5
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

/* ---------------- framework types (provided) ---------------- */
uint32_t tf_now_ms(void);                       /* HAL: free-running ms clock, wraps */

typedef enum { PLAT_FPGA = 0, PLAT_SILICON = 1 } Platform;
#define TF_SKIP_ON_FPGA  (1u << 0)              /* needs real PHY / clocks / analog */
#define TF_DESTRUCTIVE   (1u << 1)              /* burns fuses, resets the board, ... */

typedef struct {
    const char *id;          /* "PCIE-LINK-001" */
    const char *block;       /* "PCIE" */
    const char *req_id;      /* "REQ-PCIE-001" */
    int (*fn)(void *ctx);    /* 0 = pass, negative errno = fail */
    uint32_t timeout_ms;     /* 0 = no limit */
    uint32_t flags;
} TestCase;

typedef struct { const char *req_id; const char *block; } Requirement;

typedef struct {
    Platform    plat;
    const char *block_filter;    /* NULL = all blocks */
    bool        allow_destructive;
    void       *ctx;             /* passed to every test */
} RunCfg;

typedef enum { ST_PASS = 0, ST_FAIL, ST_SKIP, ST_TIMEOUT } Status;
const char *tf_status_name(Status s)      /* provided */
{
    static const char *const name[] = { "PASS", "FAIL", "SKIP", "TIMEOUT" };
    return name[s];
}

typedef struct { const TestCase *tc; Status st; int rc; uint32_t ms; } TestResult;

/* ---------------- framework: your code ---------------- */

int tf_run(const TestCase *tests, size_t n, const RunCfg *cfg, TestResult *res, size_t res_cap)
{
    /* TODO: count selected tests (-ENOSPC if they do not fit), then for each selected test:
     *       SKIP by platform/destructive flags, or run it, time it wrap-safely and classify */
    (void)tests; (void)n; (void)cfg; (void)res; (void)res_cap;
    return -ENOSYS;
}

size_t tf_report(const TestResult *res, size_t nres, const Requirement *reqs, size_t nreq,
                 char *buf, size_t cap)
{
    /* TODO: RESULT lines, SUMMARY, per-block COVERAGE, COVERAGE,ALL, UNCOVERED lines.
     *       Bounded writes (vsnprintf), return the full length like snprintf. */
    (void)res; (void)nres; (void)reqs; (void)nreq;
    if (cap > 0) buf[0] = '\0';
    return 0;
}

/* =====================================================================
 * Test harness: fake ms clock + fake tests (do not edit)
 * ===================================================================== */
static uint32_t g_now;
static uint32_t g_calls;
uint32_t tf_now_ms(void) { return g_now; }
static void busy_ms(uint32_t ms) { g_now += ms; }

static int t_link_ok(void *ctx)   { (void)ctx; g_calls++; busy_ms(12);  return 0; }
static int t_link_eio(void *ctx)  { (void)ctx; g_calls++; busy_ms(3);   return -EIO; }
static int t_ddr_train(void *ctx) { (void)ctx; g_calls++; busy_ms(40);  return 0; }   /* == timeout */
static int t_ddr_hang(void *ctx)  { (void)ctx; g_calls++; busy_ms(600); return 0; }   /* > timeout */

static const TestCase TESTS[] = {
    { "PCIE-LINK-001",  "PCIE", "REQ-PCIE-001", t_link_ok,   100, 0 },
    { "PCIE-LINK-002",  "PCIE", "REQ-PCIE-002", t_link_eio,  100, 0 },
    { "PCIE-GEN4-003",  "PCIE", "REQ-PCIE-003", t_link_ok,   100, TF_SKIP_ON_FPGA },
    { "DDR-TRAIN-001",  "DDR",  "REQ-DDR-001",  t_ddr_train,  40, 0 },
    { "DDR-STRESS-002", "DDR",  "REQ-DDR-002",  t_ddr_hang,  500, 0 },
    { "OTP-BURN-001",   "OTP",  "REQ-OTP-001",  t_link_ok,   100, TF_DESTRUCTIVE },
};
#define NT (sizeof TESTS / sizeof TESTS[0])

static const Requirement REQS[] = {
    { "REQ-PCIE-001", "PCIE" }, { "REQ-PCIE-002", "PCIE" }, { "REQ-PCIE-003", "PCIE" },
    { "REQ-DDR-001",  "DDR"  }, { "REQ-DDR-002",  "DDR"  },
    { "REQ-OTP-001",  "OTP"  },
    { "REQ-ISP-001",  "ISP"  },                  /* no test exists yet */
};
#define NR (sizeof REQS / sizeof REQS[0])

static const char EXPECT_FPGA[] =
    "RESULT,PCIE-LINK-001,PASS,0,12\n"
    "RESULT,PCIE-LINK-002,FAIL,-5,3\n"
    "RESULT,PCIE-GEN4-003,SKIP,0,0\n"
    "RESULT,DDR-TRAIN-001,PASS,0,40\n"
    "RESULT,DDR-STRESS-002,TIMEOUT,0,600\n"
    "RESULT,OTP-BURN-001,SKIP,0,0\n"
    "SUMMARY,total=6,pass=2,fail=1,skip=2,timeout=1\n"
    "COVERAGE,PCIE,1/3,33%\n"
    "COVERAGE,DDR,1/2,50%\n"
    "COVERAGE,OTP,0/1,0%\n"
    "COVERAGE,ISP,0/1,0%\n"
    "COVERAGE,ALL,2/7,28%\n"
    "UNCOVERED,REQ-PCIE-002\n"
    "UNCOVERED,REQ-PCIE-003\n"
    "UNCOVERED,REQ-DDR-002\n"
    "UNCOVERED,REQ-OTP-001\n"
    "UNCOVERED,REQ-ISP-001\n";

int main(void)
{
    TestResult res[8];
    char buf[1024];
    int k;

    /* 1. full suite on FPGA (clock wraps during the run): mixed statuses + exact report */
    g_now = 0xFFFFFFF8u; g_calls = 0;
    RunCfg fpga = { PLAT_FPGA, NULL, false, NULL };
    k = tf_run(TESTS, NT, &fpga, res, 8);
    CHECK(k == 6);
    CHECK(g_calls == 4);                                   /* 2 skipped tests not called */
    CHECK(k == 6 && res[0].st == ST_PASS && res[0].ms == 12);
    CHECK(k == 6 && res[1].st == ST_FAIL && res[1].rc == -EIO);
    CHECK(k == 6 && res[3].st == ST_PASS && res[3].ms == 40);         /* ms == timeout is OK */
    CHECK(k == 6 && res[4].st == ST_TIMEOUT && res[4].ms == 600);
    size_t len = tf_report(res, k > 0 ? (size_t)k : 0, REQS, NR, buf, sizeof buf);
    CHECK(len == strlen(EXPECT_FPGA));
    CHECK(strcmp(buf, EXPECT_FPGA) == 0);

    /* 2. silicon, destructive allowed: GEN4 and OTP now run -> 4/7 covered */
    g_now = 1000; g_calls = 0;
    RunCfg si = { PLAT_SILICON, NULL, true, NULL };
    k = tf_run(TESTS, NT, &si, res, 8);
    CHECK(k == 6 && g_calls == 6);
    CHECK(k == 6 && res[2].st == ST_PASS && res[5].st == ST_PASS);
    tf_report(res, k > 0 ? (size_t)k : 0, REQS, NR, buf, sizeof buf);
    CHECK(strstr(buf, "SUMMARY,total=6,pass=4,fail=1,skip=0,timeout=1\n") != NULL);
    CHECK(strstr(buf, "COVERAGE,PCIE,2/3,66%\n") != NULL);
    CHECK(strstr(buf, "COVERAGE,ALL,4/7,57%\n") != NULL);
    CHECK(strstr(buf, "UNCOVERED,REQ-OTP-001") == NULL);

    /* 3. block filter: only DDR tests are recorded; other blocks show as uncovered */
    g_now = 0; g_calls = 0;
    RunCfg ddr = { PLAT_FPGA, "DDR", false, NULL };
    k = tf_run(TESTS, NT, &ddr, res, 8);
    CHECK(k == 2 && g_calls == 2);
    CHECK(k == 2 && strcmp(res[0].tc->id, "DDR-TRAIN-001") == 0 && res[1].st == ST_TIMEOUT);
    tf_report(res, k > 0 ? (size_t)k : 0, REQS, NR, buf, sizeof buf);
    CHECK(strstr(buf, "SUMMARY,total=2,pass=1,fail=0,skip=0,timeout=1\n") != NULL);
    CHECK(strstr(buf, "COVERAGE,PCIE,0/3,0%\n") != NULL);
    CHECK(strstr(buf, "COVERAGE,ALL,1/7,14%\n") != NULL);

    /* 4. result buffer too small: nothing runs */
    g_calls = 0;
    CHECK(tf_run(TESTS, NT, &fpga, res, 5) == -ENOSPC && g_calls == 0);
    CHECK(tf_run(TESTS, NT, &ddr, res, 2) == 2);           /* exactly enough after filtering */

    /* 5. report buffer too small: truncated, NUL-terminated, returns the needed length */
    g_now = 0;
    k = tf_run(TESTS, NT, &fpga, res, 8);
    char small[40];
    memset(small, 'X', sizeof small);
    len = tf_report(res, k > 0 ? (size_t)k : 0, REQS, NR, small, sizeof small);
    CHECK(len == strlen(EXPECT_FPGA));
    CHECK(small[sizeof small - 1] == '\0');
    CHECK(strncmp(small, EXPECT_FPGA, sizeof small - 1) == 0);
    CHECK(tf_report(res, k > 0 ? (size_t)k : 0, REQS, NR, NULL, 0) == strlen(EXPECT_FPGA));

    /* 6. empty run: no results, every requirement uncovered, no divide-by-zero */
    len = tf_report(res, 0, NULL, 0, buf, sizeof buf);
    CHECK(strcmp(buf, "SUMMARY,total=0,pass=0,fail=0,skip=0,timeout=0\nCOVERAGE,ALL,0/0,0%\n") == 0);
    CHECK(len == strlen(buf));

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
