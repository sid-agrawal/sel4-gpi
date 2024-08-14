#pragma once

#include <sel4/sel4.h>
#include <sel4test/test.h>
#include <sel4bench/arch/sel4bench.h>

#define PRINT_BENCH_NAMES 0

// Call once at the beginning of every process that uses benchmarks
#define BENCH_UTILS_PD_INIT sel4bench_init()

// Call once at the beginning of every function that uses benchmarks
#define BENCH_UTILS_FN_INIT ccnt_t bench_start, bench_end

#define BENCH_UTILS_START()               \
    do                                    \
    {                                     \
        SEL4BENCH_READ_CCNT(bench_start); \
    } while (0)

#define BENCH_UTILS_END(name)                            \
    do                                                   \
    {                                                    \
        SEL4BENCH_READ_CCNT(bench_end);                  \
        if (PRINT_BENCH_NAMES)                           \
        {                                                \
            printf("Bench for: %s\n", name);             \
        }                                                \
        printf("RESULT>%lu\n", bench_end - bench_start); \
    } while (0)

#define BENCH_UTILS_END_NANO(name)                           \
    do                                                       \
    {                                                        \
        SEL4BENCH_READ_CCNT(bench_end);                      \
        if (PRINT_BENCH_NAMES)                               \
        {                                                    \
            printf("Bench for: %s\n", name);                 \
        }                                                    \
        printf("NANORESULT>%lu\n", bench_end - bench_start); \
    } while (0)

#define BENCH_UTILS_RECORD_NANO() \
    do                            \
    {                             \
        printf(">STARTNANO\n");   \
    } while (0)

#define BENCH_UTILS_STOP_RECORD_NANO() \
    do                                 \
    {                                  \
        printf(">STOPNANO\n");         \
    } while (0)

#define BENCH_UTILS_DESTROY sel4bench_destroy()

enum hello_benchmark_type
{
    BM_PD_CREATE = 0,
    BM_IPC,
    BM_PRINT,
    BM_DONE
};

/**
 * Print a benchmark timing result formatted for the run_benchmarks.py script
 */
// (XXX) Arya: benchmarks that use this function could be converted to the macros above
void benchmark_print_result(uint64_t result);
