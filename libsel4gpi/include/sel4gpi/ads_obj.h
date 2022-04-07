
#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

typedef struct _counter {
    seL4_Uint64 value;
}counter_t;

int counter_increment(counter_t *counter);
int counter_decrement(counter_t *counter);