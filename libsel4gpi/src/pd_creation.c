#include <sel4runtime.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/cpu_clientapi.h>
#include <sel4gpi/resource_space_clientapi.h>
#include <sel4gpi/resource_types.h>
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
    mo_client_context_t mo;
    error = mo_component_client_connect(mo_rde, n_pages, &mo);
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

pd_config_t *sel4gpi_configure_process(const char *image_name,
                                       int stack_pages,
                                       int heap_pages,
                                       sel4gpi_runnable_t *ret_runnable)
{
    int error = 0;
    PD_CREATION_PRINT("Configuring new process with image: %s\n", image_name);
    pd_config_t *proc_cfg = NULL;

    seL4_CPtr pd_rde = sel4gpi_get_rde(GPICAP_TYPE_PD);
    GOTO_IF_COND(pd_rde == seL4_CapNull, "No PD RDE\n");

    seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);
    GOTO_IF_COND(mo_rde == seL4_CapNull, "No MO RDE\n");

    pd_client_context_t self_pd_conn = sel4gpi_get_pd_conn();

    /* allocate MO for PD's OSmosis data */
    mo_client_context_t osm_data_mo;
    error = mo_component_client_connect(mo_rde, 1, &osm_data_mo);
    GOTO_IF_ERR(error, "Failed to allocat OSmosis data MO\n");

    /* new PD */
    error = pd_component_client_connect(pd_rde, &osm_data_mo, &ret_runnable->pd);
    GOTO_IF_ERR(error, "Failed to create new PD\n");
    /* new ADS*/
    seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);
    GOTO_IF_COND(ads_rde == seL4_CapNull, "Can't make new ADS, no ADS RDE\n");

    error = ads_component_client_connect(ads_rde, &ret_runnable->ads);
    GOTO_IF_ERR(error, "failed to allocate a new ADS");

    /* new CPU */
    // (XXX) Linh: for now, we'll just assume we always need a new CPU resource, configuration is TBD
    seL4_CPtr cpu_rde = sel4gpi_get_rde(GPICAP_TYPE_CPU);
    GOTO_IF_COND(cpu_rde == seL4_CapNull, "No CPU RDE\n");

    error = cpu_component_client_connect(cpu_rde, &ret_runnable->cpu);
    GOTO_IF_ERR(error, "failed to allocate a new CPU");

    proc_cfg = sel4gpi_generate_proc_config(image_name, DEFAULT_STACK_PAGES, DEFAULT_HEAP_PAGES, &osm_data_mo);
    GOTO_IF_COND(proc_cfg == NULL, "failed to generate config\n");

    // give the process its VMR RDE
    rde_config_t *vmr_rde_cfg = calloc(1, sizeof(rde_config_t));
    vmr_rde_cfg->space_id = ret_runnable->ads.id;
    vmr_rde_cfg->type = GPICAP_TYPE_VMR;

    linked_list_insert(proc_cfg->rde_cfg, vmr_rde_cfg);

err_goto:
    return proc_cfg;
}

pd_config_t *sel4gpi_configure_thread(void *thread_fn, ep_client_context_t *fault_ep, sel4gpi_runnable_t *ret_runnable)
{
    PD_CREATION_PRINT("Configuring new thread\n");
    int error = 0;
    pd_config_t *cfg = NULL;
    seL4_CPtr pd_rde = sel4gpi_get_rde(GPICAP_TYPE_PD);
    GOTO_IF_COND(pd_rde == seL4_CapNull, "No PD RDE\n");

    seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);
    GOTO_IF_COND(mo_rde == seL4_CapNull, "No MO RDE\n");

    pd_client_context_t self_pd_conn = sel4gpi_get_pd_conn();

    /* new PD */
    mo_client_context_t osm_data_mo;
    error = mo_component_client_connect(mo_rde, 1, &osm_data_mo);
    GOTO_IF_ERR(error, "Failed to allocat OSmosis data MO\n");

    error = pd_component_client_connect(pd_rde, &osm_data_mo, &ret_runnable->pd);
    GOTO_IF_ERR(error, "Failed to create new PD\n");

    /* use same ADS */
    ret_runnable->ads = sel4gpi_get_ads_conn();

    /* new CPU */
    // (XXX) Linh: for now, we'll just assume we always need a new CPU resource, configuration is TBD
    seL4_CPtr cpu_rde = sel4gpi_get_rde(GPICAP_TYPE_CPU);
    GOTO_IF_COND(cpu_rde == seL4_CapNull, "No CPU RDE\n");

    error = cpu_component_client_connect(cpu_rde, &ret_runnable->cpu);
    GOTO_IF_ERR(error, "failed to allocate a new CPU");

    cfg = sel4gpi_generate_thread_config(thread_fn, fault_ep, &osm_data_mo);

err_goto:
    return cfg;
}

/**
 * @brief given the local stack, set up the TLS for it.
 *
 * @param dest_stack address of the top of dest_stack in the current ADS
 * @param stack_pages number of pages in the stack (NOT including guard)
 * @param ipc_buffer_addr address of the ipc buffer (OPTIONAL)
 * @param osm_shared_data address of the osm_pd_shared_data_t struct
 * @param ret_sp returns the stack pointer after alignment
 * @param ret_tp returns the thread pointer after writing to the TLS
 * @return int returns 0 on success, 1 on failure
 */
static int setup_tls_in_stack(void *dest_stack,
                              size_t stack_pages,
                              void *ipc_buffer_addr,
                              void *osm_shared_data,
                              void **ret_sp,
                              void **ret_tp)
{
    int error = 0;
    size_t tls_size = sel4runtime_get_tls_size();
    /* make sure we're not going to use too much of the stack */
    GOTO_IF_COND(tls_size > stack_pages * PAGE_SIZE_4K / 8,
                 "TLS would use more than 1/8th of the application stack %zu/%zu", tls_size, stack_pages);

    uintptr_t tls_base = (uintptr_t)dest_stack - tls_size;
    uintptr_t tp = (uintptr_t)sel4runtime_write_tls_image((void *)tls_base);

    if (ipc_buffer_addr)
    {
        seL4_IPCBuffer *ipc_buf = ipc_buffer_addr;
        sel4runtime_set_tls_variable(tp, __sel4_ipc_buffer, ipc_buf);
    }

    sel4runtime_set_tls_variable(tp, __sel4gpi_osm_data, osm_shared_data);

    *ret_sp = (void *)ALIGN_DOWN(tls_base, STACK_CALL_ALIGNMENT);
    *ret_tp = (void *)tp;

    PD_CREATION_PRINT("tls base: %lx, tp: %lx, ipc_buf: %p\n", tls_base, tp, ipc_buffer_addr);
err_goto:
    return error;
}

int sel4gpi_ads_configure(ads_config_t *cfg,
                          sel4gpi_runnable_t *runnable,
                          mo_client_context_t *osm_data_mo,
                          void **ret_stack,
                          void **ret_ipc_buf,
                          void **ret_entry_point,
                          void **ret_osm_data,
                          mo_client_context_t *ret_ipc_buf_mo)
{
    int error = 0;
    ads_client_context_t vmr_rde = {.badged_server_ep_cspath.capPtr =
                                        sel4gpi_get_rde_by_space_id(runnable->ads.id, GPICAP_TYPE_VMR)};
                                        
    uint64_t current_ads_id = sel4gpi_get_binded_ads_id();
    ads_client_context_t self_ads_conn = sel4gpi_get_ads_conn();

    /* attach OSmosis data MO to the ADS*/
    if (osm_data_mo && osm_data_mo->badged_server_ep_cspath.capPtr != seL4_CapNull)
    {
        void *pd_osm_data;
        error = ads_client_attach(&vmr_rde, NULL, osm_data_mo, SEL4UTILS_RES_TYPE_GENERIC, &pd_osm_data);
        GOTO_IF_ERR(error, "Failed to attach OSmosis data MO to PD's ADS\n");

        if (ret_osm_data)
        {
            *ret_osm_data = pd_osm_data;
        }
    }

    void *auto_entry_point = NULL;

    if (cfg->vmr_cfgs)
    {
        vmr_config_t *vmr = NULL;
        for (linked_list_node_t *curr = cfg->vmr_cfgs->head; curr != NULL; curr = curr->next)
        {
            vmr = (vmr_config_t *)curr->data;
            switch (vmr->share_mode)
            {
            case GPI_COPY:
            case GPI_SHARED:
                /* shallow copying when we're in the same ADS doesn't make sense */
                if (!(current_ads_id == runnable->ads.id && vmr->share_mode == GPI_SHARED))
                {
                    PD_CREATION_PRINT("%s VMR (%s) with %lu pages at %p\n",
                                      sel4gpi_share_degree_to_str(vmr->share_mode),
                                      human_readable_va_res_type(vmr->type),
                                      vmr->region_pages, vmr->start);
                    error = ads_client_copy(&self_ads_conn, &runnable->ads, vmr);
                    GOTO_IF_ERR(error, "Failed to copy VMR (%p)\n", vmr->start);
                }
                break;
            case GPI_DISJOINT:
                if (vmr->type == SEL4UTILS_RES_TYPE_CODE)
                {
                    // elf load
                    PD_CREATION_PRINT("Loading Elf\n");
                    error = ads_client_load_elf(&runnable->ads, &runnable->pd, cfg->image_name, &auto_entry_point);
                    GOTO_IF_ERR(error, "failed to load elf to ADS");
                    WARN_IF_COND(auto_entry_point != NULL && cfg->entry_point,
                                 "Automatically found entry point (%p) differs from given one (%p)\n",
                                 auto_entry_point, cfg->entry_point);
                    WARN_IF_COND(auto_entry_point == NULL && cfg->entry_point == NULL,
                                 "PD has no entry point (either it was not found or was not given)\n");
                }
                else if (vmr->type == SEL4UTILS_RES_TYPE_STACK)
                {
                    PD_CREATION_PRINT("Allocating stack (%zu pages)\n", cfg->stack_pages);
                    void *stack = sel4gpi_new_sized_stack(&vmr_rde, cfg->stack_pages);
                    if (ret_stack)
                    {
                        *ret_stack = stack;
                    }
                    GOTO_IF_ERR(stack == NULL, "failed to allocate a new stack");
                }
                else
                {
                    PD_CREATION_PRINT("Allocating VMR (%s) with %lu pages at %p\n",
                                      human_readable_va_res_type(vmr->type), vmr->region_pages, vmr->start);
                    mo_client_context_t mo = {0};
                    void *vmr_addr = sel4gpi_get_vmr(&vmr_rde, vmr->region_pages, vmr->start, vmr->type, &mo);
                    GOTO_IF_ERR(vmr_addr == NULL, "failed to allocate VMR (%s@%p)\n", vmr->type, vmr->start);

                    if (vmr->type == SEL4UTILS_RES_TYPE_IPC_BUF)
                    {
                        if (ret_ipc_buf)
                        {
                            *ret_ipc_buf = vmr_addr;
                        }

                        if (ret_ipc_buf_mo)
                        {
                            *ret_ipc_buf_mo = mo;
                        }
                    }
                }

                break;
            default:
                GOTO_IF_COND(1, "Invalid sharing degree specified (%d) for VMR (%p)\n", vmr->share_mode, vmr->start);
                break;
            }
        }
    }

    if (ret_entry_point)
    {
        *ret_entry_point = cfg->entry_point == NULL ? auto_entry_point : cfg->entry_point;
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
            PRINT_IF_ERR(error, "Couldn't share RDE (type: %d, space ID: %d)\n", rde->type, rde->space_id);
        }
    }

err_goto:
    return error;
}

int sel4gpi_prepare_pd(pd_config_t *cfg, sel4gpi_runnable_t *runnable, int argc, seL4_Word *args)
{
    int error;

    GOTO_IF_COND(cfg == NULL || &runnable->pd == NULL, "Either no PD config given or PD to configure does not exist\n");

    seL4_CPtr free_slot;
    pd_client_context_t self_pd_conn = sel4gpi_get_pd_conn();

    seL4_Word cnode_guard = api_make_guard_skip_word(seL4_WordBits - TEST_PROCESS_CSPACE_SIZE_BITS);

    mo_client_context_t ipc_mo = {0};
    void *entry_point = NULL;
    void *stack = NULL;
    void *ipc_buf = NULL;
    void *osm_shared_data = NULL;

    error = sel4gpi_ads_configure(&cfg->ads_cfg, runnable, &cfg->osm_data_mo,
                                  &stack, &ipc_buf, &entry_point, &osm_shared_data, &ipc_mo);
    GOTO_IF_ERR(error, "Failed to configure ADS\n");

    error = rde_configure(cfg, runnable);
    GOTO_IF_ERR(error, "Failed to configure RDEs\n");

    // give the PD its CPU cap
    error = pd_client_send_core_cap(&runnable->pd, runnable->cpu.badged_server_ep_cspath.capPtr, NULL);
    GOTO_IF_ERR(error, "Failed to send CPU cap to PD\n");

    // give the PD its ADS cap
    error = pd_client_send_core_cap(&runnable->pd, runnable->ads.badged_server_ep_cspath.capPtr, NULL);
    GOTO_IF_ERR(error, "Failed to send ADS cap to PD\n");

    // give the PD its PD cap
    error = pd_client_send_core_cap(&runnable->pd, runnable->pd.badged_server_ep_cspath.capPtr, NULL);
    GOTO_IF_ERR(error, "Failed to send PD cap to PD\n");

    if (cfg->gpi_res_type_cfg)
    {
        for (linked_list_node_t *curr = cfg->gpi_res_type_cfg->head; curr != NULL; curr = curr->next)
        {
            gpi_cap_t type = (gpi_cap_t)curr->data;
            PD_CREATION_PRINT("Sharing %s Resources with PD\n", cap_type_to_str(type));
            error = pd_client_share_resource_by_type(&self_pd_conn, &runnable->pd, type);
            PRINT_IF_ERR(error, "Failed to share %s resources with PD\n", cap_type_to_str(type));
        }
    }

    if (cfg->fault_ep.badged_server_ep_cspath.capPtr == seL4_CapNull)
    {
        error = sel4gpi_alloc_endpoint(&cfg->fault_ep);
        GOTO_IF_COND(error || cfg->fault_ep.badged_server_ep_cspath.capPtr == seL4_CapNull,
                     "Couldn't allocate fault EP for PD\n");
        PD_CREATION_PRINT("Allocated new fault EP \n");
    }

    ep_client_context_t fault_ep_in_PD = {0};
    error = pd_client_send_cap(&runnable->pd,
                               cfg->fault_ep.badged_server_ep_cspath.capPtr,
                               &fault_ep_in_PD.badged_server_ep_cspath.capPtr);
    GOTO_IF_ERR(error, "Failed to send fault EP to PD\n");

    // error = ep_client_get_raw_endpoint_in_PD()
    GOTO_IF_ERR(error, "Failed to get raw EP in target PD's CSpace\n");
    PD_CREATION_PRINT("Sent fault EP to PD in slot 0x%lx\n", fault_ep_in_PD.raw_endpoint);

    // (XXX) Linh: This whole `if` blob is to be removed with unified entry-point
    if (cfg->ads_cfg.stack_shared == GPI_DISJOINT)
    {
        PD_CREATION_PRINT("Setting up runtime\n");
        pd_setup_type_t setup_mode = runnable->ads.id != sel4gpi_get_binded_ads_id() ? PD_RUNTIME_SETUP : PD_REGISTER_SETUP;

        if (setup_mode == PD_REGISTER_SETUP)
        {
            PD_CREATION_PRINT("C Runtime already initialized, setup the TLS ourselves\n");
            void *tp = NULL;
            error = setup_tls_in_stack(stack, cfg->ads_cfg.stack_pages, ipc_buf, osm_shared_data, &stack, &tp);
            GOTO_IF_ERR(error, "failed to write TLS\n");

            error = cpu_client_set_tls_base(&runnable->cpu, tp);
            GOTO_IF_ERR(error, "failed to set TLS base\n");
        }

        error = pd_client_runtime_setup(&runnable->pd,
                                        &runnable->ads,
                                        &runnable->cpu,
                                        stack,
                                        argc,
                                        args,
                                        entry_point,
                                        ipc_buf,
                                        osm_shared_data,
                                        setup_mode);
        GOTO_IF_ERR(error, "failed to prepare runtime");
    }

    PD_CREATION_PRINT("Configuring CPU Object, fault_ep: %lx\n", fault_ep_in_PD.raw_endpoint);
    error = cpu_client_config(&runnable->cpu,
                              &runnable->ads,
                              &runnable->pd,
                              &ipc_mo,
                              cnode_guard,
                              fault_ep_in_PD.raw_endpoint,
                              (seL4_Word)ipc_buf);
    GOTO_IF_ERR(error, "failed to configure CPU\n");

err_goto:
    // TODO cleanup things we've allocated
    return error;
}

int sel4gpi_start_pd(sel4gpi_runnable_t *runnable)
{
    int error = 0;
    PD_CREATION_PRINT("Starting CPU\n");
    error = cpu_client_start(&runnable->cpu);
    GOTO_IF_ERR(error, "failed to start CPU");

err_goto:
    return error;
}

pd_config_t *sel4gpi_generate_proc_config(const char *image_name, size_t stack_pages,
                                          size_t heap_pages, mo_client_context_t *osm_data_mo)
{
    pd_config_t *proc_cfg = calloc(1, sizeof(pd_config_t));
    proc_cfg->ads_cfg.code_shared = GPI_DISJOINT;
    proc_cfg->ads_cfg.stack_shared = GPI_DISJOINT;
    proc_cfg->ads_cfg.stack_pages = stack_pages;
    proc_cfg->ads_cfg.image_name = image_name;
    proc_cfg->osm_data_mo = *osm_data_mo;

    proc_cfg->ads_cfg.vmr_cfgs = linked_list_new();
    int n_cfgs = 0;
    vmr_config_t *heap_vmr = calloc(1, sizeof(vmr_config_t));
    heap_vmr->start = (void *)PD_HEAP_LOC;
    heap_vmr->type = SEL4UTILS_RES_TYPE_HEAP;
    heap_vmr->region_pages = heap_pages;
    heap_vmr->share_mode = GPI_DISJOINT;
    n_cfgs++;

    vmr_config_t *code_vmr = calloc(1, sizeof(vmr_config_t));
    code_vmr->type = SEL4UTILS_RES_TYPE_CODE;
    code_vmr->share_mode = GPI_DISJOINT;
    n_cfgs++;

    vmr_config_t *stack_vmr = calloc(1, sizeof(vmr_config_t));
    stack_vmr->type = SEL4UTILS_RES_TYPE_STACK;
    stack_vmr->share_mode = GPI_DISJOINT;
    n_cfgs++;

    vmr_config_t *ipc_buf_vmr = calloc(1, sizeof(vmr_config_t));
    ipc_buf_vmr->type = SEL4UTILS_RES_TYPE_IPC_BUF;
    ipc_buf_vmr->share_mode = GPI_DISJOINT;
    ipc_buf_vmr->region_pages = 1;
    n_cfgs++;

    linked_list_insert_many(proc_cfg->ads_cfg.vmr_cfgs, n_cfgs, code_vmr, heap_vmr, stack_vmr, ipc_buf_vmr);

    proc_cfg->rde_cfg = linked_list_new();

    int n_rde_cfgs = 0;
    rde_config_t *mo_rde_cfg = calloc(1, sizeof(rde_config_t));
    mo_rde_cfg->space_id = RESSPC_ID_NULL;
    mo_rde_cfg->type = GPICAP_TYPE_MO;
    n_rde_cfgs++;

    rde_config_t *resspc_rde_cfg = calloc(1, sizeof(rde_config_t));
    resspc_rde_cfg->space_id = RESSPC_ID_NULL;
    resspc_rde_cfg->type = GPICAP_TYPE_RESSPC;
    n_rde_cfgs++;

    linked_list_insert_many(proc_cfg->rde_cfg, n_rde_cfgs, mo_rde_cfg, resspc_rde_cfg);

    return proc_cfg;
}

pd_config_t *sel4gpi_generate_thread_config(void *thread_fn,
                                            ep_client_context_t *fault_ep,
                                            mo_client_context_t *osm_data_mo)
{
    pd_config_t *thread_cfg = calloc(1, sizeof(pd_config_t));
    if (fault_ep)
    {
        thread_cfg->fault_ep = *fault_ep;
    }
    thread_cfg->osm_data_mo = *osm_data_mo;

    ads_config_t thread_ads_cfg = {
        .entry_point = thread_fn,
        .code_shared = GPI_SHARED,
        .stack_shared = GPI_DISJOINT,
        .stack_pages = DEFAULT_STACK_PAGES};

    thread_cfg->ads_cfg = thread_ads_cfg;
    thread_cfg->ads_cfg.vmr_cfgs = linked_list_new();

    int n_cfgs = 0;
    vmr_config_t *stack_cfg = calloc(1, sizeof(vmr_config_t));
    stack_cfg->type = SEL4UTILS_RES_TYPE_STACK;
    stack_cfg->share_mode = GPI_DISJOINT;
    n_cfgs++;

    vmr_config_t *ipc_buf_cfg = calloc(1, sizeof(vmr_config_t));
    ipc_buf_cfg->type = SEL4UTILS_RES_TYPE_IPC_BUF;
    ipc_buf_cfg->share_mode = GPI_DISJOINT;
    ipc_buf_cfg->region_pages = 1;
    n_cfgs++;

    linked_list_insert_many(thread_cfg->ads_cfg.vmr_cfgs, n_cfgs, stack_cfg, ipc_buf_cfg);

    // give the thread PD all of our current RDEs
    osm_pd_shared_data_t *shared_data = ((osm_pd_shared_data_t *)sel4runtime_get_osm_shared_data());
    thread_cfg->rde_cfg = linked_list_new();
    for (int i = GPICAP_TYPE_NONE + 1; i < GPICAP_TYPE_MAX; i++)
    {
        for (int j = 0; j < MAX_NS_PER_RDE; j++)
        {
            if (shared_data->rde[i][j].type.type != GPICAP_TYPE_NONE)
            {
                rde_config_t *rde = calloc(1, sizeof(rde_config_t));
                rde->type = shared_data->rde[i][j].type.type;
                rde->space_id = shared_data->rde[i][j].space_id;
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

char *sel4gpi_share_degree_to_str(gpi_share_degree_t share_deg)
{
    switch (share_deg)
    {
    case GPI_SHARED:
        return "Shallow Copy";
    case GPI_COPY:
        return "Deep Copy";
    case GPI_DISJOINT:
        return "Disjoint";
    default:
        return "Invalid";
    }
}

void sel4gpi_add_rde_config(pd_config_t *cfg, gpi_cap_t rde_type, uint32_t space_id)
{
    rde_config_t *new_rde_cfg = calloc(1, sizeof(rde_config_t));
    if (!new_rde_cfg)
    {
        WARN("Malloc failed!\n");
        return;
    }

    new_rde_cfg->space_id = space_id;
    new_rde_cfg->type = rde_type;

    linked_list_insert(cfg->rde_cfg, new_rde_cfg);
}
