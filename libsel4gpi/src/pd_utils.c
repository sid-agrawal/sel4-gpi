#include <sel4runtime.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/pd_utils.h>

pd_client_context_t sel4gpi_get_pd_conn(void)
{
    pd_client_context_t conn = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data())->pd_conn;
    return conn;
}

ads_client_context_t sel4gpi_get_ads_conn(void)
{
    ads_client_context_t conn = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data())->ads_conn;
    return conn;
}

cpu_client_context_t sel4gpi_get_cpu_conn(void)
{
    cpu_client_context_t conn = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data())->cpu_conn;
    return conn;
}

uint64_t sel4gpi_get_binded_ads_id(void)
{
    uint64_t id = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data())->ads_conn.id;
    return id;
}

seL4_CPtr sel4gpi_get_cspace_root(void)
{
    seL4_CPtr slot = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data())->cspace_root;
    return slot;
}

seL4_CPtr sel4gpi_get_rde(int type)
{
    seL4_CPtr slot = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data())->rde[type][0].slot_in_PD;

    if (slot == seL4_CapNull)
    {
        printf("Warning: could not find RDE (type: %s) for PD (%ld)\n",
               cap_type_to_str(type), sel4gpi_get_pd_conn().id);
    }

    return slot;
}

seL4_CPtr sel4gpi_get_rde_by_space_id(uint32_t space_id, gpi_cap_t type)
{
    assert(type != GPICAP_TYPE_NONE && type != GPICAP_TYPE_MAX);
    osm_pd_init_data_t *init_data = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data());

    int idx = -1;

    for (int i = 0; i < MAX_NS_PER_RDE; i++)
    {
        if (init_data->rde[type][i].type.type == GPICAP_TYPE_NONE)
        {
            printf("Warning: could not find RDE (type: %s, space: %d) for PD (%ld)\n",
                   cap_type_to_str(type), space_id, sel4gpi_get_pd_conn().id);
            return seL4_CapNull;
        }
        else if (init_data->rde[type][i].space_id == space_id)
        {
            return init_data->rde[type][i].slot_in_PD;
        }
    }

    printf("Warning: could not find RDE (type: %s, space: %d) for PD (%ld)\n",
           cap_type_to_str(type), space_id, sel4gpi_get_pd_conn().id);
    return seL4_CapNull;
}

void sel4gpi_debug_print_rde(void)
{
    osm_pd_init_data_t *init_data = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data());
    printf("RDEs ------------------------------------ \n");
    for (int i = GPICAP_TYPE_NONE + 1; i < GPICAP_TYPE_MAX; i++)
    {
        for (int j = 0; j < MAX_NS_PER_RDE; j++)
        {
            printf("type:\t %d \t", init_data->rde[i][j].type.type);
        }
    }
}

static void sel4gpi_exit_cb(int code)
{
    /* Notify the pd component to destruct this PD */
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();
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

    pd_client_context_t self_pd = sel4gpi_get_pd_conn();

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

void *sel4gpi_new_sized_stack(ads_client_context_t *ads_rde, size_t n_pages)
{
    int error = 0;
    seL4_CPtr slot;

    pd_client_context_t self_pd = sel4gpi_get_pd_conn();

    seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);

    /* reserve one extra page for the guard */
    void *vaddr;
    error = pd_client_next_slot(&self_pd, &slot);
    GOTO_IF_ERR(error, "failed to allocate next slot\n");

    size_t res_size = (n_pages + 1) * (SIZE_BITS_TO_BYTES(MO_PAGE_BITS));
    ads_vmr_context_t reservation;
    error = ads_client_reserve(ads_rde, slot, NULL, res_size, SEL4UTILS_RES_TYPE_STACK, &reservation, &vaddr);
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

pd_resource_config_t *sel4gpi_configure_process(const char *image_name, int stack_pages, int heap_pages, pd_client_context_t *ret_pd)
{
    int error;
    pd_resource_config_t *proc_cfg = NULL;

    seL4_CPtr pd_rde = sel4gpi_get_rde(GPICAP_TYPE_PD);
    pd_client_context_t self_pd_cap = sel4gpi_get_pd_conn();

    /* new PD */
    seL4_CPtr slot;
    error = pd_client_next_slot(&self_pd_cap, &slot);
    GOTO_IF_ERR(error, "Failed to allocate slot for new PD\n");

    error = pd_component_client_connect(pd_rde, slot, ret_pd);
    GOTO_IF_ERR(error, "Failed to create new PD\n");

    proc_cfg = sel4gpi_generate_proc_config(image_name, DEFAULT_STACK_PAGES, DEFAULT_HEAP_PAGES);

    // share MO RDE by default
    error = pd_client_share_rde(ret_pd, GPICAP_TYPE_MO, RESSPC_ID_NULL);
    GOTO_IF_ERR(error, "Failed give MO RDE to new PD\n");

    // (XXX) Arya: remove this
    // Share VMR RDE by default
    error = pd_client_share_rde(ret_pd, GPICAP_TYPE_VMR, RESSPC_ID_NULL);
    GOTO_IF_ERR(error, "failed to share VMR RDE\n");

    // Share resource space RDE by default
    error = pd_client_share_rde(ret_pd, GPICAP_TYPE_RESSPC, RESSPC_ID_NULL);
    GOTO_IF_ERR(error, "failed to share resource space RDE\n");

err_goto:
    return proc_cfg;
}

static int setup_tls_in_stack(void *dest_stack, size_t stack_pages, void *ipc_buffer_addr)
{
    int error = 0;
    size_t tls_size = sel4runtime_get_tls_size();
    /* make sure we're not going to use too much of the stack */
    GOTO_IF_COND(tls_size > stack_pages * PAGE_SIZE_4K / 8, "TLS would use more than 1/8th of the application stack %zu/%zu", tls_size, stack_pages);

    uintptr_t tls_base = (uintptr_t)dest_stack - tls_size;
    uintptr_t tp = (uintptr_t)sel4runtime_write_tls_image((void *)tls_base);
    seL4_IPCBuffer *ipc_buf = ipc_buffer_addr;
    sel4runtime_set_tls_variable(tp, __sel4_ipc_buffer, ipc_buf);

    // uintptr_t aligned_stack_pointer = ALIGN_DOWN(tls_base, STACK_CALL_ALIGNMENT);

    // int error = sel4utils_arch_init_local_context(entry_point, arg0, arg1,
    //                                               (void *) thread->ipc_buffer_addr,
    //                                               (void *) aligned_stack_pointer,
    //                                               &context);

err_goto:
    return error;
}

int sel4gpi_start_pd(pd_resource_config_t *cfg, sel4gpi_runnable_t *runnable, int argc, seL4_Word *args)
{
    int error;
    seL4_CPtr free_slot;
    pd_client_context_t self_pd_cap = sel4gpi_get_pd_conn();

    // (XXX) Linh: for now, we'll just assume we always need a new CPU resource, configuration is TBD
    seL4_CPtr cpu_rde = sel4gpi_get_rde(GPICAP_TYPE_CPU);
    error = pd_client_next_slot(&self_pd_cap, &free_slot);
    GOTO_IF_ERR(error, "failed to allocate next slot");

    cpu_client_context_t new_cpu;
    error = cpu_component_client_connect(cpu_rde, free_slot, &new_cpu);
    GOTO_IF_ERR(error, "failed to allocate a new CPU");
    runnable->cpu = new_cpu;
    seL4_Word cnode_guard = api_make_guard_skip_word(seL4_WordBits - TEST_PROCESS_CSPACE_SIZE_BITS);

    ads_client_context_t target_ads = {0};
    ads_client_context_t vmr_rde = {0};
    mo_client_context_t ipc_mo = {0};
    seL4_CPtr fault_ep_in_pd;
    void *entry_point = cfg->ads_cfg.entry_point;
    void *init_stack = NULL;
    void *stack = NULL;
    void *heap = NULL;
    void *ipc_buf = NULL;
    PD_UTIL_PRINT("A\n");
    // TODO check in config is valid
    if (cfg->fault_ep != seL4_CapNull)
    {
        PD_UTIL_PRINT("B\n");
        error = pd_client_send_cap(&runnable->pd, cfg->fault_ep, &fault_ep_in_pd);
        GOTO_IF_ERR(error, "Failed to send fault EP to PD\n");
    }

    // check ADS config
    if (cfg->ads_cfg.same_ads)
    {
        vmr_rde.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_space_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_VMR);
        target_ads = sel4gpi_get_ads_conn();
    }
    else
    {
        PD_UTIL_PRINT("Making new ADS\n");
        // create a new ADS
        seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);

        /* new ADS */
        error = pd_client_next_slot(&self_pd_cap, &free_slot);
        GOTO_IF_ERR(error, "failed to allocate next slot");

        error = ads_component_client_connect(ads_rde, free_slot, &target_ads);
        GOTO_IF_ERR(error, "failed to allocate a new ADS");
        runnable->ads = target_ads;

        vmr_rde.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_space_id(target_ads.id, GPICAP_TYPE_VMR);
    }

    switch (cfg->ads_cfg.code_shared)
    {
    case GPI_SHARED:
        // shallow copy
        if (!cfg->ads_cfg.same_ads)
        {
            GOTO_IF_ERR(1, "Not implemented yet!\n");
        }
        break;
    case GPI_COPY:
        // deep copy
        GOTO_IF_ERR(1, "Not implemented yet!\n");
        break;
    case GPI_DISJOINT:
        // elf load
        PD_UTIL_PRINT("Loading Elf\n");
        error = ads_client_load_elf(&target_ads, &runnable->pd, cfg->ads_cfg.image_name, &entry_point);
        GOTO_IF_ERR(error, "failed to load elf to ADS");
        break;
    case GPI_OMIT:
        break;
    default:
        GOTO_IF_ERR(1, "Invalid sharing degree specified (%d) for code region\n", cfg->ads_cfg.code_shared);
        break;
    }

    switch (cfg->ads_cfg.stack_shared)
    {
    case GPI_SHARED:
        if (!cfg->ads_cfg.same_ads)
        {
            GOTO_IF_ERR(1, "Not implemented yet!\n");
        }
        break;
    case GPI_COPY:
        GOTO_IF_ERR(1, "Not implemented yet!\n");
        break;
    case GPI_DISJOINT:
        PD_UTIL_PRINT("Allocating stack\n");
        stack = sel4gpi_new_sized_stack(&vmr_rde, cfg->ads_cfg.stack_pages);
        GOTO_IF_ERR(stack == NULL, "failed to allocate a new stack");
        break;
    case GPI_OMIT:
        break;
    default:
        GOTO_IF_ERR(1, "Invalid sharing degree specified (%d) for stack region\n", cfg->ads_cfg.stack_shared);
        break;
    }

    switch (cfg->ads_cfg.heap_shared)
    {
    case GPI_SHARED:
        if (!cfg->ads_cfg.same_ads)
        {
            GOTO_IF_ERR(1, "Not implemented yet!\n");
        }
        break;
    case GPI_COPY:
        GOTO_IF_ERR(1, "Not implemented yet!\n");
        break;
    case GPI_DISJOINT:
        PD_UTIL_PRINT("Allocating heap\n");
        heap = sel4gpi_get_vmr(&vmr_rde, cfg->ads_cfg.heap_pages, (void *)PD_HEAP_LOC, SEL4UTILS_RES_TYPE_HEAP, NULL);
        GOTO_IF_ERR(heap == NULL, "failed to allocate a new heap");
        break;
    case GPI_OMIT:
        break;
    default:
        GOTO_IF_ERR(1, "Invalid sharing degree specified (%d) for heap region\n", cfg->ads_cfg.heap_shared);
        break;
    }

    switch (cfg->ads_cfg.ipc_buf_shared)
    {
    case GPI_SHARED:
        if (!cfg->ads_cfg.same_ads)
        {
            GOTO_IF_ERR(1, "Not implemented yet!\n");
        }
        break;
    case GPI_COPY:
        GOTO_IF_ERR(1, "Not implemented yet!\n");
        break;
    case GPI_DISJOINT:
        PD_UTIL_PRINT("Allocating IPC Buffer\n");
        ipc_buf = sel4gpi_get_vmr(&vmr_rde, 1, NULL, SEL4UTILS_RES_TYPE_IPC_BUF, &ipc_mo);
        GOTO_IF_ERR(ipc_buf == NULL, "failed to allocate a new ipc buf");
        break;
    case GPI_OMIT:
        break;
    default:
        GOTO_IF_ERR(1, "Invalid sharing degree specified (%d) for ipc buf region\n", cfg->ads_cfg.ipc_buf_shared);
        break;
    }

    PD_UTIL_PRINT("Configuring CPU Object, fault_ep: %lx\n", fault_ep_in_pd);
    error = cpu_client_config(&new_cpu, &target_ads, &runnable->pd, &ipc_mo, cnode_guard, fault_ep_in_pd, (seL4_Word)ipc_buf);
    GOTO_IF_ERR(error, "failed to configure CPU\n");

    // (XXX) Linh required that this happens after all the other setup, we can do better if we refactor the sel4utils structs out of the PD component
    if (cfg->ads_cfg.stack_shared == GPI_DISJOINT)
    {
        PD_UTIL_PRINT("Setting up stack\n");
        pd_setup_type_t setup_mode = cfg->ads_cfg.code_shared == GPI_DISJOINT ? PD_RUNTIME_SETUP : PD_REGISTER_SETUP;
        error = pd_client_runtime_setup(&runnable->pd, &target_ads, &new_cpu, stack, cfg->ads_cfg.stack_pages, argc, args, entry_point, ipc_buf, setup_mode, &init_stack);
        GOTO_IF_ERR(error, "failed to prepare stack");
    }

    // TODO loop through other VMR regions
    PD_UTIL_PRINT("Starting CPU\n");
    error = cpu_client_start(&new_cpu);
    GOTO_IF_ERR(error, "failed to start CPU");

err_goto:
    // TODO cleanup things we've allocated
    return error;
}

pd_resource_config_t *sel4gpi_generate_proc_config(const char *image_name, size_t stack_pages, size_t heap_pages)
{
    pd_resource_config_t *proc_cfg = calloc(1, sizeof(pd_resource_config_t));
    ads_resource_config_t proc_ads_cfg = {
        .same_ads = false,
        .code_shared = GPI_DISJOINT,
        .stack_shared = GPI_DISJOINT,
        .heap_shared = GPI_DISJOINT,
        .ipc_buf_shared = GPI_DISJOINT,
        .stack_pages = stack_pages,
        .heap_pages = heap_pages,
        .image_name = image_name,
        .n_vmr_shared = 0};

    proc_cfg->ads_cfg = proc_ads_cfg;

    return proc_cfg;
}

pd_resource_config_t *sel4gpi_generate_thread_config(void *thread_fn, seL4_CPtr fault_ep)
{
    pd_resource_config_t *thread_cfg = calloc(1, sizeof(pd_resource_config_t));
    thread_cfg->fault_ep = fault_ep;
    ads_resource_config_t thread_ads_cfg = {
        .same_ads = true,
        .entry_point = thread_fn,
        .code_shared = GPI_SHARED,
        .stack_shared = GPI_DISJOINT,
        .heap_shared = GPI_SHARED,
        .ipc_buf_shared = GPI_DISJOINT,
        .stack_pages = DEFAULT_STACK_PAGES,
        .heap_pages = DEFAULT_HEAP_PAGES,
        .n_vmr_shared = 0};

    thread_cfg->ads_cfg = thread_ads_cfg;

    return thread_cfg;
}
