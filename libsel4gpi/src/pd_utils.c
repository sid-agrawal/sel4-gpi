#include <sel4runtime.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/error_handle.h>
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

    seL4_CPtr slot;
    error = pd_client_next_slot(&self_pd, &slot);
    GOTO_IF_ERR(error, "failed to allocate next slot\n");

    mo_client_context_t mo;
    error = mo_component_client_connect(mo_rde, slot, num_pages, &mo);
    GOTO_IF_ERR(error, "failed to allocate MO\n");

    void *new_vaddr;
    error = ads_client_attach(ads_rde, vaddr, &mo, vmr_type, &new_vaddr);
    GOTO_IF_ERR(error, "failed to attach MO\n");

    if (ret_mo)
    {
        *ret_mo = mo;
    }

    return new_vaddr;

err_goto:
    return NULL;
}

void *sel4gpi_new_sized_stack(ads_client_context_t *ads, size_t n_pages)
{
    int error = 0;
    seL4_CPtr slot;

    pd_client_context_t self_pd;
    self_pd.badged_server_ep_cspath.capPtr = sel4gpi_get_pd_cap();

    seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);

    /* reserve one extra page for the guard */
    void *vaddr;
    error = pd_client_next_slot(&self_pd, &slot);
    GOTO_IF_ERR(error, "failed to allocate next slot\n");

    size_t res_size = (n_pages + 1) * (SIZE_BITS_TO_BYTES(MO_PAGE_BITS));
    ads_vmr_context_t reservation;
    error = ads_client_reserve(ads, slot, NULL, res_size, SEL4UTILS_RES_TYPE_STACK, &reservation, &vaddr);
    GOTO_IF_ERR(error, "failed to reserve VMR for stack\n");

    /* allocate MO */
    error = pd_client_next_slot(&self_pd, &slot);
    GOTO_IF_ERR(error, "failed to allocate next slot\n");

    mo_client_context_t mo;
    error = mo_component_client_connect(mo_rde, slot, n_pages, &mo);
    GOTO_IF_ERR(error, "failed to allocate MO\n");

    /* attach MO to ADS */
    size_t offset = SIZE_BITS_TO_BYTES(MO_PAGE_BITS);
    error = ads_client_attach_to_reserve(&reservation, &mo, offset);
    GOTO_IF_ERR(error, "failed to attach MO to reserved stack\n");

    uintptr_t stack_top = (uintptr_t)vaddr + res_size;

    return (void *)stack_top;

err_goto:
    printf("Error while allocating stack\n");
    return NULL;
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
    GOTO_IF_ERR(error, "failed to allocate next slot\n");

    pd_client_context_t new_pd;
    error = pd_component_client_connect(pd_rde, slot, &new_pd);
    GOTO_IF_ERR(error, "failed to allocate a PD\n");

    /* new ADS */
    error = pd_client_next_slot(&self_pd_cap, &slot);
    GOTO_IF_ERR(error, "failed to allocate next slot\n");

    ads_client_context_t new_ads;
    error = ads_component_client_connect(ads_rde, slot, &new_ads);
    GOTO_IF_ERR(error, "failed to allocate a new ADS\n");

    /* new CPU */
    error = pd_client_next_slot(&self_pd_cap, &slot);
    GOTO_IF_ERR(error, "failed to allocate next slot\n");

    cpu_client_context_t new_cpu;
    error = cpu_component_client_connect(cpu_rde, slot, &new_cpu);
    GOTO_IF_ERR(error, "failed to allocate a new VCPU\n");

    void *entry_point;
    error = ads_client_load_elf(&new_ads, &new_pd, image_name, &entry_point);
    GOTO_IF_ERR(error, "failed to load elf to ADS\n");

    ads_client_context_t new_ads_rde = {.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_ns_id(new_ads.id, GPICAP_TYPE_ADS)};
    void *stack = sel4gpi_new_sized_stack(&new_ads_rde, stack_pages);
    GOTO_IF_ERR(stack == NULL, "failed to allocate a new stack\n");

    void *heap = sel4gpi_get_vmr(&new_ads_rde, heap_pages, (void *)PD_HEAP_LOC, SEL4UTILS_RES_TYPE_HEAP, NULL);
    GOTO_IF_ERR(heap == NULL, "failed to allocate a new heap\n");

    mo_client_context_t ipc_mo;
    void *ipc_buf = sel4gpi_get_vmr(&new_ads_rde, 1, NULL, SEL4UTILS_RES_TYPE_IPC_BUF, &ipc_mo);
    GOTO_IF_ERR(ipc_buf == NULL, "failed to allocate a new IPC buf\n");

    seL4_Word cnode_guard = api_make_guard_skip_word(seL4_WordBits - TEST_PROCESS_CSPACE_SIZE_BITS);
    error = cpu_client_config(&new_cpu, &new_ads, &ipc_mo, &new_pd, cnode_guard, seL4_CapNull, (seL4_Word)ipc_buf);
    GOTO_IF_ERR(error, "failed to configure CPU\n");

    // Share MO RDE by default
    error = pd_client_share_rde(&new_pd, GPICAP_TYPE_MO, NSID_DEFAULT);
    GOTO_IF_ERR(error, "failed to share MO RDE\n");

    // Share resource space RDE by default
    error = pd_client_share_rde(&new_pd, GPICAP_TYPE_RESSPC, NSID_DEFAULT);
    GOTO_IF_ERR(error, "failed to share resource space RDE\n");

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

err_goto:
    // TODO cleanup allocated objects
    printf("Error occured during process spawn\n");
    return -1;
}

int sel4gpi_spawn_process(sel4gpi_process_t *proc, int argc, seL4_Word *args)
{
    int error;
    void *init_stack;
    error = ads_client_pd_setup(&proc->ads, &proc->pd, proc->stack, proc->stack_pages, argc, args, ADS_RUNTIME_SETUP, &init_stack);
    GOTO_IF_ERR(error, "failed to prepare stack");

    error = cpu_client_start(&proc->cpu, proc->entry_point, init_stack, 0);
    GOTO_IF_ERR(error, "failed to start CPU\n");

    return 0;

err_goto:
    return error;
}

int sel4gpi_configure_pd(pd_resource_config_t *cfg, pd_client_context_t *src_pd, pd_client_context_t *dst_pd, int argc, seL4_Word *args, sel4gpi_pd_t *ret_pd)
{
    int error;
    seL4_CPtr free_slot;
    pd_client_context_t self_pd_cap;
    self_pd_cap.badged_server_ep_cspath.capPtr = sel4gpi_get_pd_cap();

    // (XXX) Linh: for now, we'll just assume we always need a new CPU resource, configuration is TBD
    seL4_CPtr cpu_rde = sel4gpi_get_rde(GPICAP_TYPE_CPU);
    error = pd_client_next_slot(&self_pd_cap, &free_slot);
    GOTO_IF_ERR(error, "failed to allocate next slot");

    cpu_client_context_t new_cpu;
    error = cpu_component_client_connect(cpu_rde, free_slot, &new_cpu);
    GOTO_IF_ERR(error, "failed to allocate a new CPU");
    seL4_Word cnode_guard = api_make_guard_skip_word(seL4_WordBits - TEST_PROCESS_CSPACE_SIZE_BITS);

    ads_client_context_t new_ads = {0};
    void *entry_point = NULL;
    void *init_stack = NULL;

    // check ADS config
    if (cfg->ads_cfg.src_ads == NULL)
    {
    }
    else
    {
        seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);
        void *stack = NULL;
        void *heap = NULL;
        void *ipc_buf = NULL;
        mo_client_context_t ipc_mo;

        /* new ADS */
        error = pd_client_next_slot(&self_pd_cap, &free_slot);
        GOTO_IF_ERR(error, "failed to allocate next slot");

        error = ads_component_client_connect(ads_rde, free_slot, &new_ads);
        GOTO_IF_ERR(error, "failed to allocate a new ADS");

        ads_client_context_t new_ads_rde = {.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_ns_id(new_ads.id, GPICAP_TYPE_ADS)};

        switch (cfg->ads_cfg.code_shared)
        {
        case GPI_SHARED:
            // shallow copy
            printf("Not implemented yet!\n");
            break;
        case GPI_COPY:
            // deep copy
            printf("Not implemented yet!\n");
            break;
        case GPI_DISJOINT:
            // elf load
            error = ads_client_load_elf(&new_ads, dst_pd, cfg->ads_cfg.image_name, &entry_point);
            GOTO_IF_ERR(error, "failed to load elf to ADS");
            break;
        default:
            break;
        }

        if (cfg->ads_cfg.stack_shared == GPI_DISJOINT)
        {
            stack = sel4gpi_new_sized_stack(&new_ads_rde, cfg->ads_cfg.stack_pages);
            GOTO_IF_ERR(stack == NULL, "failed to allocate a new stack");
        }

        if (cfg->ads_cfg.heap_shared == GPI_DISJOINT)
        {
            heap = sel4gpi_get_vmr(&new_ads_rde, cfg->ads_cfg.heap_pages, (void *)PD_HEAP_LOC, SEL4UTILS_RES_TYPE_HEAP, NULL);
            GOTO_IF_ERR(heap == NULL, "failed to allocate a new heap");
        }

        if (cfg->ads_cfg.ipc_buf_shared == GPI_DISJOINT)
        {
            ipc_buf = sel4gpi_get_vmr(&new_ads_rde, 1, NULL, SEL4UTILS_RES_TYPE_IPC_BUF, &ipc_mo);
            GOTO_IF_ERR(ipc_buf == NULL, "failed to allocate a new IPC buf");
        }

        error = cpu_client_config(&new_cpu, &new_ads, ipc_buf == NULL ? NULL : &ipc_mo, dst_pd, cnode_guard, seL4_CapNull, (seL4_Word)ipc_buf);
        GOTO_IF_ERR(error, "failed to configure CPU");

        // (XXX) Linh required that this happens after all the other setup, we can do better if we refactor the sel4utils structs out of the PD component
        if (cfg->ads_cfg.stack_shared)
        {
            ads_setup_type_t setup_mode = cfg->ads_cfg.code_shared == GPI_DISJOINT ? ADS_RUNTIME_SETUP : ADS_TLS_SETUP;
            error = ads_client_pd_setup(&new_ads, dst_pd, stack, cfg->ads_cfg.stack_pages, argc, args, setup_mode, &init_stack);
            GOTO_IF_ERR(error, "failed to prepare stack");
        }
    }

    // TODO loop through other VMR regions

    error = cpu_client_start(&new_cpu, entry_point, init_stack, 0);
    GOTO_IF_ERR(error, "failed to start CPU");

    if (ret_pd)
    {
        ret_pd->cpu = new_cpu;
        ret_pd->ads = new_ads;
    }

err_goto:
    // TODO cleanup things we've allocated
    return error;
}

pd_resource_config_t *sel4gpi_generate_proc_config(char *image_name)
{
    pd_resource_config_t *proc_cfg = malloc(sizeof(pd_resource_config_t));
    ads_resource_config_t proc_ads_cfg = {
        .code_shared = GPI_DISJOINT,
        .stack_shared = GPI_DISJOINT,
        .heap_shared = GPI_DISJOINT,
        .ipc_buf_shared = GPI_DISJOINT,
        .stack_pages = DEFAULT_STACK_PAGES,
        .heap_pages = DEFAULT_HEAP_PAGES,
        .image_name = image_name,
        .n_vmr_shared = 0};

    proc_cfg->ads_cfg = proc_ads_cfg;

    return proc_cfg;
}
