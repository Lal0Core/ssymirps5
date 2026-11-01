#include <pthread.h>
#include <stdio.h>
#include <stdatomic.h>
#define _umtx_op my_umtx_op
#define __udivti3 my_udivti3
#include "../tooling/ymir/ps5_core_shims.c"
/* Ping-pong between threads through wait/notify on 64-bit words, like libc++'s
 * std::atomic<T>::wait/notify_all do over _umtx_op. A lost wakeup hangs. */
enum { THREADS = 8, ROUNDS = 20000 };
static _Atomic long turn;
static void *worker(void *arg) {
    long id = (long)arg;
    for (long r = 0; r < ROUNDS; r++) {
        long want = r * THREADS + id, cur;
        while ((cur = atomic_load(&turn)) != want)
            my_umtx_op((void *)&turn, 2 /* WAIT */, (unsigned long)cur, 0, 0);
        atomic_store(&turn, want + 1);
        my_umtx_op((void *)&turn, 3 /* WAKE */, 0x7fffffff, 0, 0);
    }
    return 0;
}
int main(void) {
    pthread_t t[THREADS];
    for (long i = 0; i < THREADS; i++) pthread_create(&t[i], 0, worker, (void *)i);
    for (int i = 0; i < THREADS; i++) pthread_join(t[i], 0);
    printf("umtx ping-pong: %s (turn=%ld)\n", atomic_load(&turn) == (long)THREADS * ROUNDS ? "PASS" : "FAIL", atomic_load(&turn));
}
