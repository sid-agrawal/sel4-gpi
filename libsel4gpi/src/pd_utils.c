#include <sel4runtime.h>
#include <sel4gpi/pd_clientapi.h>

#include <sel4gpi/pd_utils.h>

seL4_CPtr sel4gpi_get_pd_cap(void)
{
    seL4_CPtr slot = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data())->pd_cap;
    return slot;
}

seL4_CPtr sel4gpi_get_ads_cap(void)
{
    seL4_CPtr slot = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data())->ads_cap;
    return slot;
}

seL4_CPtr sel4gpi_get_cpu_cap(void)
{
    seL4_CPtr slot = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data())->cpu_cap;
    return slot;
}

seL4_CPtr sel4gpi_get_rde(int type)
{
    seL4_CPtr slot = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data())->rde[type][0].slot_in_PD;
    return slot;
}

uint64_t sel4gpi_get_binded_ads_id(void)
{
    return ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data())->binded_ads_ns_id;
}

seL4_CPtr sel4gpi_get_rde_by_ns_id(uint32_t ns_id, gpi_cap_t type)
{
    assert(type != GPICAP_TYPE_NONE && type != GPICAP_TYPE_MAX);
    osm_pd_init_data_t *init_data = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data());

    int idx = -1;

    for (int i = 0; i < MAX_NS_PER_RDE; i++)
    {
        if (init_data->rde[type][i].type.type == GPICAP_TYPE_NONE)
        {
            return seL4_CapNull;
        }
        else if (init_data->rde[type][i].ns_id == ns_id)
        {
            return init_data->rde[type][i].slot_in_PD;
        }
    }

    return seL4_CapNull;
}

uintptr_t sel4gpi_setup_thread_stack(void *stack_addr, size_t stack_pages)
{
    seL4_UserContext regs = {0};
    size_t context_size = sizeof(seL4_UserContext) / sizeof(seL4_Word);

    size_t tls_size = sel4runtime_get_tls_size();
    /* make sure we're not going to use too much of the stack */
    if (tls_size > stack_pages * PAGE_SIZE_4K / 8)
    {
        ZF_LOGE("TLS would use more than 1/8th of the application stack %zu/%zu", tls_size, stack_pages);
        return -1;
    }
    uintptr_t tls_base = (uintptr_t)stack_addr - tls_size;
    uintptr_t tp = (uintptr_t)sel4runtime_write_tls_image((void *)tls_base);
    return ALIGN_DOWN(tls_base, STACK_CALL_ALIGNMENT);
}
