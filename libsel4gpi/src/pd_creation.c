#include <sel4runtime.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/pd_creation.h>
#include <sel4gpi/pd_utils.h>

void *sel4gpi_new_sized_stack(ads_client_context_t *ads_rde, size_t n_pages)
{
    int error = 0;
    seL4_CPtr slot;

    pd_client_context_t self_pd = sel4gpi_get_pd_conn();

    seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);
    GOTO_IF_COND(mo_rde == seL4_CapNull, "Can't allocate stack, no MO RDE\n");

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

pd_config_t *sel4gpi_configure_process(const char *image_name, int stack_pages, int heap_pages, pd_client_context_t *ret_pd)
{
    int error;
    pd_config_t *proc_cfg = NULL;

    seL4_CPtr pd_rde = sel4gpi_get_rde(GPICAP_TYPE_PD);
    GOTO_IF_COND(pd_rde == seL4_CapNull, "No PD RDE\n");

    pd_client_context_t self_pd_conn = sel4gpi_get_pd_conn();

    /* new PD */
    seL4_CPtr slot;
    error = pd_client_next_slot(&self_pd_conn, &slot);
    GOTO_IF_ERR(error, "Failed to allocate slot for new PD\n");

    error = pd_component_client_connect(pd_rde, slot, ret_pd);
    GOTO_IF_ERR(error, "Failed to create new PD\n");

    proc_cfg = sel4gpi_generate_proc_config(image_name, DEFAULT_STACK_PAGES, DEFAULT_HEAP_PAGES);

err_goto:
    return proc_cfg;
}

/**
 * @brief given the local stack, set up the TLS for it.
 *
 * @param dest_stack address of the top of dest_stack in the current ADS
 * @param stack_pages number of pages in the stack (NOT including guard)
 * @param ipc_buffer_addr address of the ipc buffer (OPTIONAL)
 * @param osm_init_data address of the osm_pd_init_data_t struct
 * @param ret_sp returns the stack pointer after alignment
 * @param ret_tp returns the thread pointer after writing to the TLS
 * @return int returns 0 on success, 1 on failure
 */
static int setup_tls_in_stack(void *dest_stack, size_t stack_pages, void *ipc_buffer_addr, void *osm_init_data, void **ret_sp, void **ret_tp)
{
    int error = 0;
    size_t tls_size = sel4runtime_get_tls_size();
    /* make sure we're not going to use too much of the stack */
    GOTO_IF_COND(tls_size > stack_pages * PAGE_SIZE_4K / 8, "TLS would use more than 1/8th of the application stack %zu/%zu", tls_size, stack_pages);

    uintptr_t tls_base = (uintptr_t)dest_stack - tls_size;
    uintptr_t tp = (uintptr_t)sel4runtime_write_tls_image((void *)tls_base);

    if (ipc_buffer_addr)
    {
        seL4_IPCBuffer *ipc_buf = ipc_buffer_addr;
        sel4runtime_set_tls_variable(tp, __sel4_ipc_buffer, ipc_buf);
    }

    sel4runtime_set_tls_variable(tp, __sel4gpi_osm_data, osm_init_data);

    *ret_sp = (void *)ALIGN_DOWN(tls_base, STACK_CALL_ALIGNMENT);
    *ret_tp = (void *)tp;

    PD_CREATION_PRINT("tls base: %lx, tp: %lx, ipc_buf: %p\n", tls_base, tp, ipc_buffer_addr);
err_goto:
    return error;
}

static int ads_configure(pd_config_t *cfg,
                         sel4gpi_runnable_t *runnable,
                         void **ret_stack,
                         void **ret_ipc_buf,
                         void **ret_entry_point,
                         mo_client_context_t *ret_ipc_buf_mo)
{
    int error = 0;
    ads_client_context_t vmr_rde = {0};
    pd_client_context_t self_pd_conn = sel4gpi_get_pd_conn();
    seL4_CPtr free_slot;

    if (cfg->ads_cfg.same_ads)
    {
        vmr_rde.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_space_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_VMR);
        runnable->ads = sel4gpi_get_ads_conn();
    }
    else
    {
        PD_CREATION_PRINT("Making new ADS\n");
        // create a new ADS
        seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);
        GOTO_IF_COND(ads_rde == seL4_CapNull, "Can't make new ADS, no ADS RDE\n");

        /* new ADS */
        error = pd_client_next_slot(&self_pd_conn, &free_slot);
        GOTO_IF_ERR(error, "failed to allocate next slot");

        error = ads_component_client_connect(ads_rde, free_slot, &runnable->ads);
        GOTO_IF_ERR(error, "failed to allocate a new ADS");

        vmr_rde.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_space_id(runnable->ads.id, GPICAP_TYPE_VMR);
    }

    void *auto_entry_point = NULL;
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
        PD_CREATION_PRINT("Loading Elf\n");
        error = ads_client_load_elf(&runnable->ads, &runnable->pd, cfg->ads_cfg.image_name, &auto_entry_point);
        GOTO_IF_ERR(error, "failed to load elf to ADS");
        break;
    case GPI_OMIT:
        break;
    default:
        GOTO_IF_ERR(1, "Invalid sharing degree specified (%d) for code region\n", cfg->ads_cfg.code_shared);
        break;
    }

    if (cfg->ads_cfg.entry_point)
    {
        *ret_entry_point = cfg->ads_cfg.entry_point;
        PRINT_IF_COND(auto_entry_point != NULL, COLORIZE("Warning:", CYAN) "Overriding automatically found entry point (%p) with given one (%p)\n", auto_entry_point, cfg->ads_cfg.entry_point);
    }
    else
    {
        *ret_entry_point = auto_entry_point;
    }

    GOTO_IF_COND(*ret_entry_point == NULL, "PD has no entry point (either it was not found or was not given)\n");

    if (cfg->ads_cfg.vmr_cfgs)
    {
        vmr_config_t *vmr = NULL;
        for (linked_list_node_t *curr = cfg->ads_cfg.vmr_cfgs->head; curr != NULL; curr = curr->next)
        {
            vmr = (vmr_config_t *)curr->data;
            switch (vmr->share_mode)
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
                PD_CREATION_PRINT("Allocating VMR (%s) with %lu pages at %p\n", human_readable_va_res_type(vmr->type), vmr->region_pages, vmr->start);
                void *vmr_addr = sel4gpi_get_vmr(&vmr_rde, vmr->region_pages, vmr->start, vmr->type, NULL);
                GOTO_IF_ERR(vmr_addr == NULL, "failed to allocate VMR (%p)\n", vmr->start);
                break;
            case GPI_OMIT:
                break;
            default:
                GOTO_IF_ERR(1, "Invalid sharing degree specified (%d) for VMR (%p)\n", vmr->share_mode, vmr->start);
                break;
            }
        }
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
        PD_CREATION_PRINT("Allocating stack (%zu pages)\n", cfg->ads_cfg.stack_pages);
        *ret_stack = sel4gpi_new_sized_stack(&vmr_rde, cfg->ads_cfg.stack_pages);
        GOTO_IF_ERR(*ret_stack == NULL, "failed to allocate a new stack");
        break;
    case GPI_OMIT:
        break;
    default:
        GOTO_IF_ERR(1, "Invalid sharing degree specified (%d) for stack region\n", cfg->ads_cfg.stack_shared);
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
        PD_CREATION_PRINT("Allocating IPC Buffer\n");
        *ret_ipc_buf = sel4gpi_get_vmr(&vmr_rde, 1, NULL, SEL4UTILS_RES_TYPE_IPC_BUF, ret_ipc_buf_mo);
        GOTO_IF_ERR(*ret_ipc_buf == NULL, "failed to allocate a new ipc buf");
        break;
    case GPI_OMIT:
        break;
    default:
        GOTO_IF_ERR(1, "Invalid sharing degree specified (%d) for ipc buf region\n", cfg->ads_cfg.ipc_buf_shared);
        break;
    }

err_goto:
    return error;
}

static int rde_configure(pd_config_t *cfg, sel4gpi_runnable_t *runnable)
{
    int error = 0;
    pd_client_context_t self_pd_conn = sel4gpi_get_pd_conn();

    rde_config_t *rde;
    if (cfg->rde_cfg)
    {
        for (linked_list_node_t *curr = cfg->rde_cfg->head; curr != NULL; curr = curr->next)
        {
            rde = (rde_config_t *)curr->data;
            PD_CREATION_PRINT("Sharing RDE (type: %s, space ID: %d)\n", cap_type_to_str(rde->type), rde->space_id);
            error = pd_client_share_rde(&runnable->pd, rde->type, rde->space_id);
            PRINT_IF_ERR(error, "Couldn't share RDE (type: %s, space ID: %d)\n", cap_type_to_str(rde->type), rde->space_id);
        }
    }

err_goto:
    return error;
}

int sel4gpi_start_pd(pd_config_t *cfg, sel4gpi_runnable_t *runnable, int argc, seL4_Word *args)
{
    int error;

    GOTO_IF_COND(cfg == NULL || &runnable->pd == NULL, "Either no PD config given or PD to configure does not exist\n");

    seL4_CPtr free_slot;
    pd_client_context_t self_pd_conn = sel4gpi_get_pd_conn();

    // (XXX) Linh: for now, we'll just assume we always need a new CPU resource, configuration is TBD
    seL4_CPtr cpu_rde = sel4gpi_get_rde(GPICAP_TYPE_CPU);
    GOTO_IF_COND(cpu_rde == seL4_CapNull, "No CPU RDE\n");

    error = pd_client_next_slot(&self_pd_conn, &free_slot);
    GOTO_IF_ERR(error, "failed to allocate next slot");

    error = cpu_component_client_connect(cpu_rde, free_slot, &runnable->cpu);
    GOTO_IF_ERR(error, "failed to allocate a new CPU");
    seL4_Word cnode_guard = api_make_guard_skip_word(seL4_WordBits - TEST_PROCESS_CSPACE_SIZE_BITS);

    mo_client_context_t ipc_mo = {0};
    seL4_CPtr fault_ep_in_pd;
    void *entry_point = NULL;
    void *stack = NULL;
    void *ipc_buf = NULL;
    void *osm_init_data = NULL;

    error = ads_configure(cfg, runnable, &stack, &ipc_buf, &entry_point, &ipc_mo);
    GOTO_IF_ERR(error, "Failed to configure ADS\n");

    error = rde_configure(cfg, runnable);
    GOTO_IF_ERR(error, "Failed to configure RDEs\n");

    if (cfg->gpi_res_type_cfg)
    {
        PD_CREATION_PRINT("Sharing Resources\n");
        for (linked_list_node_t *curr = cfg->gpi_res_type_cfg->head; curr != NULL; curr = curr->next)
        {
            gpi_cap_t type = (gpi_cap_t)curr->data;
            error = pd_client_share_resource_by_type(&self_pd_conn, &runnable->pd, type);
            PRINT_IF_ERR(error, "Failed to share %s resources with PD\n", cap_type_to_str(type));
        }
    }

    // TODO check in config is valid
    if (cfg->fault_ep != seL4_CapNull)
    {
        PD_CREATION_PRINT("Sending fault EP (0x%lx) to PD\n", cfg->fault_ep);
        error = pd_client_send_cap(&runnable->pd, cfg->fault_ep, &fault_ep_in_pd);
        GOTO_IF_ERR(error, "Failed to send fault EP to PD\n");
    }
    else
    {
        error = pd_client_alloc_ep(&runnable->pd, &fault_ep_in_pd);
        PD_CREATION_PRINT("Allocated new fault EP at 0x%lx\n", fault_ep_in_pd);
        GOTO_IF_ERR(error, "Couldn't allocate fault EP for PD\n");
    }

    PD_CREATION_PRINT("Configuring CPU Object, fault_ep: %lx\n", fault_ep_in_pd);
    error = cpu_client_config(&runnable->cpu, &runnable->ads, &runnable->pd, &ipc_mo, cnode_guard, fault_ep_in_pd, (seL4_Word)ipc_buf, &osm_init_data);
    GOTO_IF_ERR(error, "failed to configure CPU\n");

    // (XXX) Linh required that this happens after cpu_client_config specifically due to dependency between the sel4util structs in the GPI components
    if (cfg->ads_cfg.stack_shared == GPI_DISJOINT)
    {
        PD_CREATION_PRINT("Setting up runtime\n");
        pd_setup_type_t setup_mode = cfg->ads_cfg.code_shared == GPI_DISJOINT ? PD_RUNTIME_SETUP : PD_REGISTER_SETUP;

        if (setup_mode == PD_REGISTER_SETUP)
        {
            PD_CREATION_PRINT("C Runtime already initialized, setup the TLS ourselves\n");
            void *tp = NULL;
            error = setup_tls_in_stack(stack, cfg->ads_cfg.stack_pages, ipc_buf, osm_init_data, &stack, &tp);
            GOTO_IF_ERR(error, "failed to write TLS\n");

            error = cpu_client_set_tls_base(&runnable->cpu, tp);
            GOTO_IF_ERR(error, "failed to set TLS base\n");
        }

        error = pd_client_runtime_setup(&runnable->pd, &runnable->ads, &runnable->cpu, stack, cfg->ads_cfg.stack_pages, argc, args, entry_point, ipc_buf, setup_mode);
        GOTO_IF_ERR(error, "failed to prepare runtime");
    }

    PD_CREATION_PRINT("Starting CPU\n");
    error = cpu_client_start(&runnable->cpu);
    GOTO_IF_ERR(error, "failed to start CPU");

err_goto:
    // TODO cleanup things we've allocated
    return error;
}

pd_config_t *sel4gpi_generate_proc_config(const char *image_name, size_t stack_pages, size_t heap_pages)
{
    pd_config_t *proc_cfg = calloc(1, sizeof(pd_config_t));
    proc_cfg->ads_cfg.same_ads = false;
    proc_cfg->ads_cfg.code_shared = GPI_DISJOINT;
    proc_cfg->ads_cfg.ipc_buf_shared = GPI_DISJOINT;
    proc_cfg->ads_cfg.stack_shared = GPI_DISJOINT;
    proc_cfg->ads_cfg.stack_pages = stack_pages;
    proc_cfg->ads_cfg.image_name = image_name;

    proc_cfg->ads_cfg.vmr_cfgs = linked_list_new();
    vmr_config_t *heap_vmr = calloc(1, sizeof(vmr_config_t));
    heap_vmr->start = (void *)PD_HEAP_LOC;
    heap_vmr->type = SEL4UTILS_RES_TYPE_HEAP;
    heap_vmr->region_pages = heap_pages;
    heap_vmr->share_mode = GPI_DISJOINT;

    linked_list_insert(proc_cfg->ads_cfg.vmr_cfgs, heap_vmr);

    proc_cfg->rde_cfg = linked_list_new();

    rde_config_t *mo_rde_cfg = calloc(1, sizeof(rde_config_t));
    mo_rde_cfg->space_id = RESSPC_ID_NULL;
    mo_rde_cfg->type = GPICAP_TYPE_MO;

    rde_config_t *resspc_rde_cfg = calloc(1, sizeof(rde_config_t));
    resspc_rde_cfg->space_id = RESSPC_ID_NULL;
    resspc_rde_cfg->type = GPICAP_TYPE_RESSPC;

    linked_list_insert(proc_cfg->rde_cfg, mo_rde_cfg);
    linked_list_insert(proc_cfg->rde_cfg, resspc_rde_cfg);

    return proc_cfg;
}

pd_config_t *sel4gpi_generate_thread_config(void *thread_fn, seL4_CPtr fault_ep)
{
    pd_config_t *thread_cfg = calloc(1, sizeof(pd_config_t));
    thread_cfg->fault_ep = fault_ep;
    ads_config_t thread_ads_cfg = {
        .same_ads = true,
        .entry_point = thread_fn,
        .code_shared = GPI_SHARED,
        .ipc_buf_shared = GPI_DISJOINT,
        .stack_shared = GPI_DISJOINT,
        .stack_pages = DEFAULT_STACK_PAGES};

    thread_cfg->ads_cfg = thread_ads_cfg;

    // give the thread PD all of our current RDEs
    osm_pd_init_data_t *init_data = ((osm_pd_init_data_t *)sel4runtime_get_osm_init_data());
    thread_cfg->rde_cfg = linked_list_new();
    for (int i = GPICAP_TYPE_NONE + 1; i < GPICAP_TYPE_MAX; i++)
    {
        for (int j = 0; j < MAX_NS_PER_RDE; j++)
        {
            if (init_data->rde[i][j].type.type != GPICAP_TYPE_NONE)
            {
                rde_config_t *rde = calloc(1, sizeof(rde_config_t));
                rde->type = init_data->rde[i][j].type.type;
                rde->space_id = init_data->rde[i][j].space_id;
                linked_list_insert(thread_cfg->rde_cfg, rde);
            }
        }
    }

    // since we're sharing address spaces, transfer all MO caps to the thread PD
    thread_cfg->gpi_res_type_cfg = linked_list_new();
    linked_list_insert(thread_cfg->gpi_res_type_cfg, (void *)GPICAP_TYPE_MO);

    return thread_cfg;
}

void sel4gpi_config_destroy(pd_config_t *cfg)
{
    if (cfg->ads_cfg.vmr_cfgs)
    {
        for (linked_list_node_t *curr = cfg->ads_cfg.vmr_cfgs->head; curr != NULL; curr = curr->next)
        {
            free((vmr_config_t *)curr->data);
        }
        linked_list_destroy(cfg->ads_cfg.vmr_cfgs);
    }

    if (cfg->rde_cfg)
    {
        for (linked_list_node_t *curr = cfg->rde_cfg->head; curr != NULL; curr = curr->next)
        {
            free((rde_config_t *)curr->data);
        }
        linked_list_destroy(cfg->rde_cfg);
    }

    if (cfg->gpi_res_type_cfg)
    {
        linked_list_destroy(cfg->gpi_res_type_cfg);
    }

    free(cfg);
}