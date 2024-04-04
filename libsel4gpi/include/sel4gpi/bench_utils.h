#pragma once

enum hello_benchmark_type
{
    BM_PD_CREATE = 0,
    BM_IPC
};

char *get_bench_type_name(bool native);
