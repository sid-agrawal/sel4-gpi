
int client_cap_increment(sel4_gpi_counter_cap_t counter_cap, int increment) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, increment);
    seL4_Call(counter_cap, tag);
    return seL4_GetMR(0);
}