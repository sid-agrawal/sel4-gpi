#include <sel4runtime.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/mo_clientapi.h>

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

static void sel4gpi_exit_cb(int code)
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

void *sel4gpi_get_vmr(ads_client_context_t *ads_rde, int num_pages, void *vaddr, sel4utils_reservation_type_t vmr_type, mo_client_context_t *ret_mo)
{
    int error;

    pd_client_context_t self_pd;
    self_pd.badged_server_ep_cspath.capPtr = sel4gpi_get_pd_cap();

    seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);

    /* allocate stack frame */
    seL4_CPtr slot;
    error = pd_client_next_slot(&self_pd, &slot);
    GOTO_IF_ERR(error);

    mo_client_context_t mo;
    error = mo_component_client_connect(mo_rde, slot, num_pages, &mo);
    GOTO_IF_ERR(error);

    /* attach stack to cpu */
    void *new_vaddr;
    error = ads_client_attach(ads_rde, vaddr, &mo, vmr_type, &new_vaddr);
    GOTO_IF_ERR(error);

    if (ret_mo)
    {
        *ret_mo = mo;
    }

    return new_vaddr;

error:
    printf("ERROR\n");
    return NULL;
}

void *sel4gpi_new_sized_stack(ads_client_context_t *ads, size_t n_pages)
{
    int error = 0;

    /* one extra page for the guard */
    void *vaddr = sel4gpi_get_vmr(ads, n_pages, NULL, SEL4UTILS_RES_TYPE_STACK, NULL);
    if (vaddr == NULL)
    {
        return NULL;
    }

    uintptr_t stack_top = (uintptr_t)vaddr + (n_pages * PAGE_SIZE_4K);

    return (void *)stack_top;
}

int sel4gpi_configure_process(const char *image_name, int stack_pages, int heap_pages, sel4gpi_process_t *ret_proc)
{
    int error;
    pd_client_context_t self_pd_cap;
    self_pd_cap.badged_server_ep_cspath.capPtr = sel4gpi_get_pd_cap();

    seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);
    seL4_CPtr pd_rde = sel4gpi_get_rde(GPICAP_TYPE_PD);
    seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);
    seL4_CPtr cpu_rde = sel4gpi_get_rde(GPICAP_TYPE_CPU);

    /* new PD */
    seL4_CPtr slot;
    error = pd_client_next_slot(&self_pd_cap, &slot);
    GOTO_IF_ERR(error);
    pd_client_context_t new_pd;
    error = pd_component_client_connect(pd_rde, slot, &new_pd);
    GOTO_IF_ERR(error);

    /* new ADS */
    error = pd_client_next_slot(&self_pd_cap, &slot);
    GOTO_IF_ERR(error);
    ads_client_context_t new_ads;
    error = ads_component_client_connect(ads_rde, slot, &new_ads);
    GOTO_IF_ERR(error);

    /* new CPU */
    error = pd_client_next_slot(&self_pd_cap, &slot);
    GOTO_IF_ERR(error);
    cpu_client_context_t new_cpu;
    error = cpu_component_client_connect(cpu_rde, slot, &new_cpu);
    GOTO_IF_ERR(error);

    void *entry_point;
    error = ads_client_load_elf(&new_ads, &new_pd, image_name, &entry_point);
    GOTO_IF_ERR(error);

    ads_client_context_t new_ads_rde = {.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_ns_id(new_ads.id, GPICAP_TYPE_ADS)};
    void *stack = sel4gpi_new_sized_stack(&new_ads_rde, stack_pages);
    test_assert(stack != NULL);

    void *heap = sel4gpi_get_vmr(&new_ads_rde, heap_pages, (void *)PD_HEAP_LOC, SEL4UTILS_RES_TYPE_HEAP, NULL);
    test_assert(heap != NULL);

    mo_client_context_t ipc_mo;
    void *ipc_buf = sel4gpi_get_vmr(&new_ads_rde, 1, NULL, SEL4UTILS_RES_TYPE_IPC_BUF, &ipc_mo);
    test_assert(ipc_buf != NULL);

    seL4_Word cnode_guard = api_make_guard_skip_word(seL4_WordBits - TEST_PROCESS_CSPACE_SIZE_BITS);
    error = cpu_client_config(&new_cpu, &new_ads, &ipc_mo, &new_pd, cnode_guard, seL4_CapNull, (seL4_Word)ipc_buf);
    GOTO_IF_ERR(error);

    error = pd_client_share_rde(&new_pd, GPICAP_TYPE_MO, NSID_DEFAULT);
    GOTO_IF_ERR(error);

    // error = cpu_client_start(&new_cpu, entry_point, init_stack, 0);
    // GOTO_IF_ERR(error);

    if (ret_proc)
    {
        ret_proc->pd = new_pd;
        ret_proc->ads = new_ads;
        ret_proc->cpu = new_cpu;
        ret_proc->entry_point = entry_point;
        ret_proc->stack = stack;
        ret_proc->stack_pages = stack_pages;
    }

    return 0;
error:
    // TODO cleanup allocated objects
    printf("Error occured during process spawn\n");
    return -1;
}

int sel4gpi_spawn_process(sel4gpi_process_t *proc, int argc, seL4_Word *args)
{
    int error;
    void *init_stack;
    error = ads_client_prepare_stack(&proc->ads, &proc->pd, proc->stack, proc->stack_pages, argc, args, &init_stack);
    GOTO_IF_ERR(error);

    error = cpu_client_start(&proc->cpu, proc->entry_point, init_stack, 0);
    GOTO_IF_ERR(error);

    return 0;
error:
    return error;
}
