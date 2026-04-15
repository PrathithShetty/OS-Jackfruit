#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static size_t parse_size_mb(const char *arg, size_t fallback)
{
    char *end = NULL;
    unsigned long value = strtoul(arg, &end, 10);

    if (!arg || *arg == '\0' || (end && *end != '\0') || value == 0)
        return fallback;
    return (size_t)value;
}

static useconds_t parse_sleep_ms(const char *arg, useconds_t fallback)
{
    char *end = NULL;
    unsigned long value = strtoul(arg, &end, 10);

    if (!arg || *arg == '\0' || (end && *end != '\0'))
        return fallback;

    return (useconds_t)(value * 1000);
}

int main(int argc, char *argv[])
{
    size_t chunk_mb = (argc > 1) ? parse_size_mb(argv[1], 10) : 10;
    useconds_t sleep_us = (argc > 2) ? parse_sleep_ms(argv[2], 500) : 500000;

    size_t chunk_bytes = chunk_mb * 1024 * 1024;
    int count = 0;

    printf("Memory hog starting...\n");

    while (1) {
        char *mem = malloc(chunk_bytes);
        if (!mem) {
            printf("malloc failed after %d allocations\n", count);
            break;
        }

        memset(mem, 'A', chunk_bytes);

        count++;
        printf("allocation=%d chunk=%zuMB total=%zuMB\n",
               count, chunk_mb, (size_t)count * chunk_mb);

        fflush(stdout);
        usleep(sleep_us);
    }

    return 0;
}
