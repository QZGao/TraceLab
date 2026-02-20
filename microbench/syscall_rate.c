#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

// Simple syscall-rate microbenchmark:
// issues many explicit syscalls via syscall(2).
int main(int argc, char **argv) {
    long iterations = 100000;
    if (argc >= 2) {
        iterations = strtol(argv[1], NULL, 10);
    }
    if (iterations <= 0) {
        fprintf(stderr, "usage: %s [iterations>0]\n", argv[0]);
        return 2;
    }

    long accumulator = 0;
    for (long i = 0; i < iterations; ++i) {
        accumulator += syscall(SYS_getpid);
        if ((i & 15) == 0) {
            accumulator += syscall(SYS_getppid);
        }
    }

    printf("syscall_rate iterations=%ld accumulator_mod=%ld\n", iterations,
           accumulator % 1000000L);
    return 0;
}
