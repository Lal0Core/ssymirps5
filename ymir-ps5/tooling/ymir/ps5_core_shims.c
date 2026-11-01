/* PS5 core-local runtime pieces for the Ymir libretro core.
 *
 * PS5_RetroArch's loader resolves a core's imports only from a table generated
 * from the cores the title shipped with. These are names this core's private
 * libc++/libunwind need that no shipped core is known to import, so the core
 * defines them itself (hidden by the version script). */
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#define HIDDEN __attribute__((visibility("hidden")))

/* ---- _umtx_op: libc++'s std::atomic wait/notify on FreeBSD ----------------
 * libc++ (src/atomic.cpp) calls _umtx_op(addr, UMTX_OP_WAIT, expected, 0, 0) to
 * sleep while the 64-bit value at addr equals expected, and
 * _umtx_op(addr, UMTX_OP_WAKE, count, 0, 0) to wake sleepers. Emulated with a
 * hashed table of mutex/condition pairs: a waiter checks the value with its
 * bucket locked and waits on the bucket, and a waker broadcasts with the bucket
 * locked after the value changed, so no wakeup is lost. Spurious wakeups are
 * allowed: libc++ re-checks the value in a loop. */
enum { UMTX_OP_WAIT = 2, UMTX_OP_WAKE = 3, UMTX_BUCKETS = 64 };

static struct umtx_bucket {
    pthread_mutex_t lock;
    pthread_cond_t cond;
} umtx_buckets[UMTX_BUCKETS];
static pthread_once_t umtx_once = PTHREAD_ONCE_INIT;

static void umtx_init(void) {
    for (int i = 0; i < UMTX_BUCKETS; ++i) {
        pthread_mutex_init(&umtx_buckets[i].lock, NULL);
        pthread_cond_init(&umtx_buckets[i].cond, NULL);
    }
}

static struct umtx_bucket *umtx_bucket_for(const void *address) {
    uintptr_t key = (uintptr_t)address >> 3;
    key ^= key >> 7;
    return &umtx_buckets[key % UMTX_BUCKETS];
}

HIDDEN int _umtx_op(void *obj, int op, unsigned long val, void *uaddr, void *uaddr2) {
    (void)uaddr, (void)uaddr2;
    pthread_once(&umtx_once, umtx_init);
    struct umtx_bucket *b = umtx_bucket_for(obj);
    switch (op) {
    case UMTX_OP_WAIT:
        pthread_mutex_lock(&b->lock);
        if (__atomic_load_n((volatile unsigned long *)obj, __ATOMIC_SEQ_CST) == val)
            pthread_cond_wait(&b->cond, &b->lock);
        pthread_mutex_unlock(&b->lock);
        return 0;
    case UMTX_OP_WAKE:
        /* A bucket is shared by several addresses, so wake every sleeper on it:
         * an unrelated waiter only re-checks its value and sleeps again. */
        pthread_mutex_lock(&b->lock);
        pthread_cond_broadcast(&b->cond);
        pthread_mutex_unlock(&b->lock);
        return 0;
    default:
        return -1;
    }
}

/* ---- __udivti3: unsigned 128-bit division (compiler helper) ---------------- */
static int clz128(unsigned __int128 x) {
    const uint64_t hi = (uint64_t)(x >> 64), lo = (uint64_t)x;
    return hi ? __builtin_clzll(hi) : 64 + __builtin_clzll(lo);
}

HIDDEN unsigned __int128 __udivti3(unsigned __int128 n, unsigned __int128 d) {
    if (d == 0)
        __builtin_trap();
    if ((n >> 64) == 0 && (d >> 64) == 0)
        return (uint64_t)n / (uint64_t)d;
    if (d > n)
        return 0;
    /* Shift-subtract: align the divisor's top bit with the dividend's (n >= d > 0). */
    const int shift = clz128(d) - clz128(n);
    unsigned __int128 q = 0, dd = d << shift;
    for (int i = shift; i >= 0; --i) {
        q <<= 1;
        if (n >= dd) {
            n -= dd;
            q |= 1;
        }
        dd >>= 1;
    }
    return q;
}
