#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Simple memory-bandwidth microbenchmark:
// write + read passes over a contiguous buffer.
int main(int argc, char **argv) {
    size_t mib = 64;
    int passes = 4;
    if (argc >= 2) {
        mib = (size_t)strtoull(argv[1], NULL, 10);
    }
    if (argc >= 3) {
        passes = atoi(argv[2]);
    }
    if (mib == 0 || passes <= 0) {
        fprintf(stderr, "usage: %s [mib>0] [passes>0]\n", argv[0]);
        return 2;
    }

    const size_t bytes = mib * 1024ULL * 1024ULL;
    unsigned char *buf = (unsigned char *)malloc(bytes);
    if (buf == NULL) {
        perror("malloc");
        return 1;
    }

    uint64_t checksum = 0;
    for (int p = 0; p < passes; ++p) {
        for (size_t i = 0; i < bytes; ++i) {
            buf[i] = (unsigned char)((i + (size_t)p) & 0xFFU);
        }
        for (size_t i = 0; i < bytes; ++i) {
            checksum += (uint64_t)buf[i];
        }
    }

    printf("mem_bw checksum=%llu bytes=%llu passes=%d\n",
           (unsigned long long)checksum, (unsigned long long)bytes, passes);
    free(buf);
    return 0;
}

