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

    // Share VMR RDE by default
    error = pd_client_share_rde(ret_pd, GPICAP_TYPE_VMR, RESSPC_ID_NULL);
    GOTO_IF_ERR(error, "failed to share VMR RDE\n");

    // Share resource space RDE by default
    error = pd_client_share_rde(ret_pd, GPICAP_TYPE_RESSPC, RESSPC_ID_NULL);
    GOTO_IF_ERR(error, "failed to share resource space RDE\n");

err_goto:
    return proc_cfg;
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

    ads_client_context_t new_ads = {0};
    void *entry_point = NULL;
    void *init_stack = NULL;

    // check ADS config
    if (cfg->ads_cfg.same_ads)
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
        runnable->ads = new_ads;

        ads_client_context_t new_ads_rde = {.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_space_id(new_ads.id, GPICAP_TYPE_VMR)};

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
            error = ads_client_load_elf(&new_ads, &runnable->pd, cfg->ads_cfg.image_name, &entry_point);
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

        error = cpu_client_config(&new_cpu, &new_ads, ipc_buf == NULL ? NULL : &ipc_mo, &runnable->pd, cnode_guard, seL4_CapNull, (seL4_Word)ipc_buf);
        GOTO_IF_ERR(error, "failed to configure CPU");

        // (XXX) Linh required that this happens after all the other setup, we can do better if we refactor the sel4utils structs out of the PD component
        if (cfg->ads_cfg.stack_shared)
        {
            ads_setup_type_t setup_mode = cfg->ads_cfg.code_shared == GPI_DISJOINT ? ADS_RUNTIME_SETUP : ADS_TLS_SETUP;
            error = ads_client_pd_setup(&new_ads, &runnable->pd, &new_cpu, stack, cfg->ads_cfg.stack_pages, argc, args, setup_mode, &init_stack);
            GOTO_IF_ERR(error, "failed to prepare stack");
        }
    }

    // TODO loop through other VMR regions

    error = cpu_client_start(&new_cpu, entry_point, init_stack, 0);
    GOTO_IF_ERR(error, "failed to start CPU");

err_goto:
    // TODO cleanup things we've allocated
    return error;
}

pd_resource_config_t *sel4gpi_generate_proc_config(char *image_name, size_t stack_pages, size_t heap_pages)
{
    pd_resource_config_t *proc_cfg = malloc(sizeof(pd_resource_config_t));
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

pd_resource_config_t *sel4gpi_generate_thread_config(void)
{
    pd_resource_config_t *thread_cfg = malloc(sizeof(pd_resource_config_t));
    ads_resource_config_t thread_ads_cfg = {
        .same_ads = true,
        .code_shared = GPI_SHARED,
        .stack_shared = GPI_DISJOINT,
        .heap_shared = GPI_SHARED,
        .ipc_buf_shared = GPI_SHARED,
        .stack_pages = DEFAULT_STACK_PAGES,
        .heap_pages = DEFAULT_HEAP_PAGES,
        .n_vmr_shared = 0};

    thread_cfg->ads_cfg = thread_ads_cfg;
}
