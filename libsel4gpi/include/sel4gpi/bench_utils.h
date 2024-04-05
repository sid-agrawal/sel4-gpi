#pragma once

enum hello_benchmark_type
{
    BM_PD_CREATE = 0,
    BM_IPC,
    BM_PRINT,
    BM_DONE
};

char *get_bench_type_name(bool native);
