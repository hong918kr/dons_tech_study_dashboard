/* crash.c — TOOL-M11 victim program (Mac + Linux).
 * Three named tasks share one heap object (struct session):
 *   rx_parser  copies a 24-byte name from a "packet" into name[16]  -> clobbers s->tx
 *   tx_worker  later flushes s->tx                                   -> SIGSEGV here
 *   watchdog   just kicks a counter and sleeps
 * The order is forced with a mutex + condvar, so every run crashes the same way.
 * build (Mac):   cc -std=c11 -g -O0 -Wall -Wextra -Werror crash.c -o crash
 * build (Linux): gcc -std=c11 -g -O0 -Wall -Wextra -Werror -pthread crash.c -o crash
 */
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SESSION_MAGIC 0x5E55C0DEu

struct txbuf {
	uint32_t len;
	uint8_t data[60];
};

struct session {               /* the heap object every task touches */
	uint32_t magic;
	uint16_t id;
	uint16_t state;
	char name[16];             /* filled from the wire */
	struct txbuf *tx;          /* right after name[] -> first victim of an overflow */
	uint32_t seq;
	uint32_t flags;
};

struct task {                  /* like an RTOS TCB: the name lives in memory */
	char name[16];
	void *(*entry)(void *);
	pthread_t th;
	volatile uint32_t loops;
};

struct pkt {
	uint8_t type;
	uint8_t len;               /* length field from the sender: NOT trusted */
	char payload[30];
};

/* breadcrumb ring: who did what, last TRACE_N events (firmware style) */
#define TRACE_N 8
struct trace_ev {
	uint32_t seq;
	char who[12];
	char what[12];
	uint32_t arg;
};
struct trace_ev g_trace[TRACE_N];
uint32_t g_trace_seq;

struct session *g_sess;
struct task g_tasks[3];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static int g_parsed;

static void trace(const char *who, const char *what, uint32_t arg)
{
	pthread_mutex_lock(&g_lock);
	struct trace_ev *e = &g_trace[g_trace_seq % TRACE_N];
	e->seq = g_trace_seq++;
	snprintf(e->who, sizeof e->who, "%s", who);
	snprintf(e->what, sizeof e->what, "%s", what);
	e->arg = arg;
	pthread_mutex_unlock(&g_lock);
}

static void set_thread_name(const char *name)
{
#ifdef __APPLE__
	pthread_setname_np(name);
#else
	pthread_setname_np(pthread_self(), name);
#endif
}

/* ---- rx_parser: the writer (bug site) ---- */
static void parse_name(struct session *s, const struct pkt *p)
{
	memcpy(s->name, p->payload, p->len);   /* BUG: p->len (24) > sizeof s->name (16) */
	s->seq++;
}

static void *rx_parser_main(void *arg)
{
	struct task *self = arg;
	set_thread_name(self->name);
	struct pkt p = { .type = 1, .len = 24 };
	memcpy(p.payload, "front-left-camera-module", 24);
	uint32_t last_len = p.len;
	trace(self->name, "rx_name", last_len);
	parse_name(g_sess, &p);
	self->loops++;
	pthread_mutex_lock(&g_lock);
	g_parsed = 1;
	pthread_cond_broadcast(&g_cv);
	for (;;)                               /* wait for the next packet (never comes) */
		pthread_cond_wait(&g_cv, &g_lock);
	return NULL;
}

/* ---- tx_worker: the victim (crash site) ---- */
static uint32_t tx_flush(struct session *s)
{
	struct txbuf *tx = s->tx;
	uint32_t n = tx->len;                  /* SIGSEGV: tx = "a-module" bytes */
	tx->len = 0;
	return n;
}

static void *tx_worker_main(void *arg)
{
	struct task *self = arg;
	set_thread_name(self->name);
	pthread_mutex_lock(&g_lock);
	while (!g_parsed)
		pthread_cond_wait(&g_cv, &g_lock);
	pthread_mutex_unlock(&g_lock);
	trace(self->name, "tx_flush", g_sess->seq);
	uint32_t sent = tx_flush(g_sess);
	self->loops += sent;
	return NULL;
}

/* ---- watchdog: innocent bystander ---- */
static void *watchdog_main(void *arg)
{
	struct task *self = arg;
	set_thread_name(self->name);
	for (;;) {
		self->loops++;
		usleep(1000);
	}
	return NULL;
}

int main(void)
{
	g_sess = calloc(1, sizeof *g_sess);
	g_sess->magic = SESSION_MAGIC;
	g_sess->id = 7;
	g_sess->state = 2;
	g_sess->tx = calloc(1, sizeof *g_sess->tx);
	g_sess->tx->len = 12;
	trace("main", "boot", (uint32_t)getpid());
	printf("crash: pid %d, session %p, tx %p\n", (int)getpid(), (void *)g_sess, (void *)g_sess->tx);
	fflush(stdout);

	static const struct { const char *name; void *(*fn)(void *); } def[3] = {
		{ "watchdog", watchdog_main },
		{ "tx_worker", tx_worker_main },
		{ "rx_parser", rx_parser_main },
	};
	for (int i = 0; i < 3; i++) {
		snprintf(g_tasks[i].name, sizeof g_tasks[i].name, "%s", def[i].name);
		g_tasks[i].entry = def[i].fn;
		pthread_create(&g_tasks[i].th, NULL, def[i].fn, &g_tasks[i]);
	}
	pthread_join(g_tasks[1].th, NULL);
	puts("not reached");
	return 0;
}
