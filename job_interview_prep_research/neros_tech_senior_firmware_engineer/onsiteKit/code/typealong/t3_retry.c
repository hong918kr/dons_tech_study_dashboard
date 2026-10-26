/* T3 command / ACK / timeout / retry -- "no ACK in 50 ms -> resend, 3 tries total, ids are one byte" */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define TIMEOUT_MS 50u
#define MAX_TRIES 3u
#define MAX_INFLIGHT 8u
#define NO_ID (-1)

typedef void (*send_fn)(uint8_t id, uint8_t payload);

typedef struct {
    bool used;
    uint8_t payload, tries;
    uint32_t deadline;        /* ms */
} slot_t;

typedef struct {
    slot_t slot[256];         /* indexed by the 8-bit id: no search, no malloc */
    unsigned inflight;
    uint8_t next_id;          /* uint8_t: 255 + 1 wraps to 0 by itself */
    uint32_t stale_acks;
    send_fn send;
} sender_t;

/* wrap-safe "now >= deadline" for a 32-bit ms clock (wraps after 49.7 days) */
static bool expired(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

int submit(sender_t *s, uint8_t payload, uint32_t now)
{
    if (s->inflight >= MAX_INFLIGHT)
        return NO_ID;                         /* back-pressure, caller decides */
    while (s->slot[s->next_id].used)          /* skip ids still waiting */
        s->next_id++;
    uint8_t id = s->next_id++;
    s->slot[id] = (slot_t){true, payload, 1, now + TIMEOUT_MS};
    s->inflight++;
    s->send(id, payload);
    return id;
}

bool on_ack(sender_t *s, uint8_t id)
{
    if (!s->slot[id].used) {
        s->stale_acks++;                      /* late / duplicate / unknown */
        return false;
    }
    s->slot[id].used = false;
    s->inflight--;
    return true;
}

/* resend what timed out; write ids that ran out of tries to failed[], return how many */
unsigned tick(sender_t *s, uint32_t now, uint8_t *failed)
{
    unsigned nf = 0;
    for (unsigned id = 0; id < 256; id++) {
        slot_t *c = &s->slot[id];
        if (!c->used || !expired(now, c->deadline))
            continue;
        if (c->tries >= MAX_TRIES) {
            c->used = false;
            s->inflight--;
            failed[nf++] = (uint8_t)id;
        } else {
            c->tries++;
            c->deadline = now + TIMEOUT_MS;
            s->send((uint8_t)id, c->payload);
        }
    }
    return nf;
}

/* ---- tests ---- */
static int sent;
static void fake_send(uint8_t id, uint8_t payload) { (void)id; (void)payload; sent++; }

int main(void)
{
    static sender_t s = {.send = fake_send};
    uint8_t failed[256];
    int a = submit(&s, 7, 1000);
    assert(tick(&s, 1049, failed) == 0 && sent == 1);      /* just before deadline */
    assert(tick(&s, 1050, failed) == 0 && sent == 2);      /* exactly at deadline -> resend */
    assert(tick(&s, 1100, failed) == 0 && sent == 3);
    assert(tick(&s, 1150, failed) == 1 && failed[0] == a && s.inflight == 0);
    assert(!on_ack(&s, (uint8_t)a) && s.stale_acks == 1);   /* late ACK */
    int b = submit(&s, 8, 2000);
    assert(on_ack(&s, (uint8_t)b) && !on_ack(&s, (uint8_t)b)); /* duplicate ACK */

    static sender_t w = {.send = fake_send};
    w.next_id = 255;
    assert(submit(&w, 1, 0) == 255 && submit(&w, 2, 0) == 0);  /* id wraps */
    for (int i = 0; i < 6; i++)
        submit(&w, 0, 0);
    assert(submit(&w, 0, 0) == NO_ID);                      /* inflight full */

    static sender_t z = {.send = fake_send};
    submit(&z, 1, 0xFFFFFFC0u);                              /* deadline 0xFFFFFFF2, clock about to wrap */
    assert(tick(&z, 0xFFFFFFF1u, failed) == 0 && z.slot[0].tries == 1);  /* 1 ms early: wait */
    assert(tick(&z, 0x10, failed) == 0 && z.slot[0].tries == 2);  /* after wrap: naive now >= deadline misses this */
    puts("t3 OK");
    return 0;
}
