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

seL4_CPtr sel4gpi_get_cspace_root(void)
{
    seL4_CPtr slot = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data())->cspace_root;
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

static sel4gpi_exit_cb(int code)
{
    /* Notify the pd component to destruct this PD */
    pd_client_context_t pd_conn;
    pd_conn.badged_server_ep_cspath.capPtr = sel4gpi_get_pd_cap();
    pd_client_exit(&pd_conn);
}

void sel4gpi_set_exit_cb(void)
{
    /* Set exit handler */
    sel4runtime_set_exit(sel4gpi_exit_cb);
}

void *sel4gpi_create_stack(int num_pages)
{
    int error;

    pd_client_context_t self_pd;
    self_pd.badged_server_ep_cspath.capPtr = sel4gpi_get_pd_cap();

    ads_client_context_t vmr_rde;
    vmr_rde.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_ns_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_ADS);

    seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);

    /* allocate stack frame */
    seL4_CPtr slot;
    error = pd_client_next_slot(&self_pd, &slot);
    assert(error == 0);

    mo_client_context_t stack_mo;
    error = mo_component_client_connect(mo_rde, slot, num_pages, &stack_mo);
    assert(error == 0);

    /* attach stack to cpu */
    void *stack_addr_in_new_cpu;
    error = ads_client_attach(&vmr_rde, NULL, &stack_mo, &stack_addr_in_new_cpu);
    assert(error == 0);

    return stack_addr_in_new_cpu;
}
