#pragma once

#include <sel4/sel4.h>
#include <sel4test/test.h>

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
void benchmark_print_result(uint64_t result);
