#include <stdbool.h>
#include <sel4gpi/bench_utils.h>

char *get_bench_type_name(bool native)
{
    return native ? "Native" : "Cellulos";
}
