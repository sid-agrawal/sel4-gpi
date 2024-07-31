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

extern void _start(void);

void *sel4gpi_new_sized_stack(ads_client_context_t *ads_rde, size_t n_pages, mo_client_context_t *ret_mo)
{
    int error = 0;

    pd_client_context_t self_pd = sel4gpi_get_pd_conn();

    seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);
    GOTO_IF_COND(mo_rde == seL4_CapNull, "Can't allocate stack, no MO RDE\n");

    /* reserve one extra page for the guard */
    void *vaddr;
    size_t res_size = (n_pages + 1) * (SIZE_BITS_TO_BYTES(MO_PAGE_BITS));
    ads_vmr_context_t reservation;
    error = ads_client_reserve(ads_rde, NULL, res_size, MO_PAGE_BITS,
                               SEL4UTILS_RES_TYPE_STACK, &reservation, &vaddr);
    GOTO_IF_ERR(error, "failed to reserve VMR for stack\n");

    /* allocate MO */
    mo_client_context_t mo;
    error = mo_component_client_connect(mo_rde, n_pages, MO_PAGE_BITS, &mo);
    GOTO_IF_ERR(error, "failed to allocate MO\n");

    /* attach MO to ADS */
    size_t offset = SIZE_BITS_TO_BYTES(MO_PAGE_BITS);
    error = ads_client_attach_to_reserve(&reservation, &mo, offset);
    GOTO_IF_ERR(error, "failed to attach MO to reserved stack\n");

    uintptr_t stack_top = (uintptr_t)vaddr + res_size;

    if (ret_mo)
    {
        *ret_mo = mo;
    }

    return (void *)stack_top;

err_goto:
    printf("Error while allocating stack\n");
    return NULL;
}

pd_config_t *sel4gpi_new_runnable(bool new_ads, bool new_cpu, sel4gpi_runnable_t *ret_runnable)
{
    assert(ret_runnable != NULL);

    int error = 0;
    seL4_CPtr pd_rde = sel4gpi_get_rde(GPICAP_TYPE_PD);
    GOTO_IF_COND(pd_rde == seL4_CapNull, "No PD RDE\n");

    seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);
    GOTO_IF_COND(mo_rde == seL4_CapNull, "No MO RDE\n");

    pd_client_context_t self_pd_conn = sel4gpi_get_pd_conn();

    /* allocate MO for PD's OSmosis data */
    mo_client_context_t osm_data_mo;
    error = mo_component_client_connect(mo_rde, 1, MO_PAGE_BITS, &osm_data_mo);
    GOTO_IF_ERR(error, "Failed to allocat OSmosis data MO\n");

    /* new PD */
    error = pd_component_client_connect(pd_rde, &osm_data_mo, &ret_runnable->pd);
    GOTO_IF_ERR(error, "Failed to create new PD\n");

    if (new_ads)
    {
        /* new ADS*/
        seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);
        GOTO_IF_COND(ads_rde == seL4_CapNull, "Can't make new ADS, no ADS RDE\n");

        error = ads_component_client_connect(ads_rde, &ret_runnable->ads);
        GOTO_IF_ERR(error, "failed to allocate a new ADS");
    }

    if (new_cpu)
    {
        /* new CPU */
        seL4_CPtr cpu_rde = sel4gpi_get_rde(GPICAP_TYPE_CPU);
        GOTO_IF_COND(cpu_rde == seL4_CapNull, "No CPU RDE\n");

        error = cpu_component_client_connect(cpu_rde, &ret_runnable->cpu);
        GOTO_IF_ERR(error, "failed to allocate a new CPU");
    }

    pd_config_t *cfg = calloc(1, sizeof(pd_config_t));
    cfg->osm_data_mo = osm_data_mo;

    return cfg;

err_goto:
    return NULL;
}

pd_config_t *sel4gpi_configure_process(const char *image_name,
                                       int stack_pages,
                                       int heap_pages,
                                       sel4gpi_runnable_t *ret_runnable)
{
    int error = 0;
    assert(image_name != NULL);
    PD_CREATION_PRINT("Configuring new process with image: %s\n", image_name);

    pd_config_t *proc_cfg = sel4gpi_new_runnable(true, true, ret_runnable);
    GOTO_IF_COND(proc_cfg == NULL, "Failed to allocated components needed for execution\n");

    sel4gpi_generate_proc_config(proc_cfg, image_name, DEFAULT_STACK_PAGES, DEFAULT_HEAP_PAGES);

    // give the process its VMR RDE
    rde_config_t *vmr_rde_cfg = calloc(1, sizeof(rde_config_t));
    assert(vmr_rde_cfg != NULL);
    vmr_rde_cfg->space_id = ret_runnable->ads.id;
    vmr_rde_cfg->type = GPICAP_TYPE_VMR;

    linked_list_insert(proc_cfg->rde_cfg, vmr_rde_cfg);

    return proc_cfg;

err_goto:
    if (proc_cfg)
    {
        free(proc_cfg);
    }

    // Not cleaning up elements that may have been allocated, they will be left in ret_runnable

    return NULL;
}

pd_config_t *sel4gpi_configure_thread(void *thread_fn, ep_client_context_t *fault_ep, sel4gpi_runnable_t *ret_runnable)
{
    PD_CREATION_PRINT("Configuring new thread\n");
    int error = 0;

    pd_config_t *cfg = sel4gpi_new_runnable(false, true, ret_runnable);
    GOTO_IF_COND(cfg == NULL, "Failed to allocated components needed for execution\n");
    ret_runnable->ads = sel4gpi_get_ads_conn();

    sel4gpi_generate_thread_config(cfg, thread_fn, fault_ep);

err_goto:
    return cfg;
}

static void write_tls_values(uintptr_t stack_top,
                             void *ipc_buffer_addr,
                             void *osm_shared_data,
                             uintptr_t *ret_tls_base,
                             uintptr_t *ret_tp)
{
    size_t tls_size = sel4runtime_get_tls_size();
    uintptr_t tls_base = stack_top - tls_size;
    uintptr_t tp = (uintptr_t)sel4runtime_write_tls_image((void *)tls_base);

    if (ipc_buffer_addr)
    {
        seL4_IPCBuffer *ipc_buf = ipc_buffer_addr;
        sel4runtime_set_tls_variable(tp, __sel4_ipc_buffer, ipc_buf);
    }

    sel4runtime_set_tls_variable(tp, __sel4gpi_osm_data, osm_shared_data);

    *ret_tls_base = tls_base;
    *ret_tp = tp;
}

/**
 * @brief given belonging to an ADS with the same code segment as the current one,
 * set up the TLS for it. If the given ADS is a different one, the stack
 * will be mapped into the current one, then unmapped.
 *
 * @param target_ads the ADS which the stack will execute in
 * @param runtime_ctxt the runtime addresses and MOs determined during ADS configuration
 * @param ret_sp returns the stack pointer after alignment
 * @param ret_tp returns the thread pointer after writing to the TLS
 * @return int returns 0 on success, 1 on failure
 */
static int setup_tls_in_stack(ads_client_context_t *target_ads,
                              runtime_context_t *runtime_ctxt,
                              void **ret_sp,
                              void **ret_tp)
{
    int error = 0;
    size_t stack_pages = runtime_ctxt->stack_cfg->region_pages;
    size_t tls_size = sel4runtime_get_tls_size();

    GOTO_IF_COND(stack_pages == 0, "Stack VMR page count needed\n");
    GOTO_IF_COND(runtime_ctxt->stack_cfg->start == NULL, "No stack region exists to write TLS\n");
    /* make sure we're not going to use too much of the stack */
    GOTO_IF_COND(tls_size > stack_pages * PAGE_SIZE_4K / 8,
                 "TLS would use more than 1/8th of the application stack %zu/%zu", tls_size, stack_pages);

    uintptr_t curr_sp, tls_base, tp = 0;
    if (target_ads->id != sel4gpi_get_binded_ads_id())
    {
        ads_client_context_t vmr_rde = sel4gpi_get_bound_vmr_rde();
        void *local_stack_bottom = NULL;
        error = ads_client_attach(&vmr_rde, NULL, &runtime_ctxt->stack_cfg->mo,
                                  SEL4UTILS_RES_TYPE_GENERIC, &local_stack_bottom);
        GOTO_IF_ERR(error, "Failed to map remote stack into current ADS\n");

        uintptr_t remote_tls_base, remote_tp, local_stack_top = 0;

        // set stack pointer to the top
        curr_sp = (uintptr_t)local_stack_bottom + stack_pages * SIZE_BITS_TO_BYTES(MO_PAGE_BITS);
        local_stack_top = curr_sp;
        write_tls_values(curr_sp, runtime_ctxt->ipc_buf_cfg->start, runtime_ctxt->osm_data, &tls_base, &tp);

        remote_tls_base = (uintptr_t)runtime_ctxt->stack_cfg->start - (local_stack_top - tls_base);
        remote_tp = (uintptr_t)runtime_ctxt->stack_cfg->start - (local_stack_top - tp);
        *ret_sp = (void *)ALIGN_DOWN(remote_tls_base, STACK_CALL_ALIGNMENT);
        *ret_tp = (void *)remote_tp;

        ads_client_rm(&vmr_rde, local_stack_bottom);
    }
    else
    {
        curr_sp = (uintptr_t)runtime_ctxt->stack_cfg->start;
        write_tls_values(curr_sp, runtime_ctxt->ipc_buf_cfg->start, runtime_ctxt->osm_data, &tls_base, &tp);
        *ret_sp = (void *)ALIGN_DOWN(tls_base, STACK_CALL_ALIGNMENT);
        *ret_tp = (void *)tp;
    }

err_goto:

    return error;
}

int sel4gpi_ads_configure(ads_config_t *cfg,
                          sel4gpi_runnable_t *runnable,
                          mo_client_context_t *osm_data_mo,
                          runtime_context_t *ret_runtime_context)
{
    int error = 0;
    ads_client_context_t vmr_rde = {.ep = sel4gpi_get_rde_by_space_id(runnable->ads.id, GPICAP_TYPE_VMR)};

    gpi_obj_id_t current_ads_id = sel4gpi_get_binded_ads_id();
    ads_client_context_t self_ads_conn = sel4gpi_get_ads_conn();

    /* attach OSmosis data MO to the ADS*/
    if (osm_data_mo && osm_data_mo->ep != seL4_CapNull)
    {
        void *pd_osm_data;
        error = ads_client_attach(&vmr_rde, NULL, osm_data_mo, SEL4UTILS_RES_TYPE_SHARED_FRAMES, &pd_osm_data);
        GOTO_IF_ERR(error, "Failed to attach OSmosis data MO to PD's ADS\n");

        if (ret_runtime_context)
        {
            ret_runtime_context->osm_data = pd_osm_data;
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
            case GPI_SHARED:
                /* shallow copying when we're in the same ADS doesn't make sense */
                if (current_ads_id != runnable->ads.id)
                {
                    error = ads_client_shallow_copy(&self_ads_conn, &runnable->ads, vmr);
                    GOTO_IF_ERR(error, "Failed to copy VMR (%p)\n", vmr->start);
                    PD_CREATION_PRINT("%s VMR (%s)\n",
                                      sel4gpi_share_degree_to_str(vmr->share_mode),
                                      human_readable_va_res_type(vmr->type));
                    if (vmr->start != NULL && vmr->region_pages > 0)
                    {
                        PD_CREATION_PRINT_2("with %lu pages at %p\n",
                                            vmr->region_pages, vmr->start);
                    }
                }
                break;
            case GPI_DISJOINT:
                // If an MO is provided for stack and code regions, they will not be allocated like below
                if (vmr->mo.ep != seL4_CapNull)
                {
                    PD_CREATION_PRINT("Attaching provided MO for a %s VMR\n", human_readable_va_res_type(vmr->type));
                    void *vmr_addr = NULL;
                    error = ads_client_attach(&vmr_rde, vmr->start, &vmr->mo, vmr->type, &vmr_addr);
                    GOTO_IF_ERR(error, "Failed to attach MO to VMR\n");
                    vmr->start = vmr_addr;
                    // (XXX) Linh: should we also give ownership of this MO to the created PD?
                }
                else if (vmr->type == SEL4UTILS_RES_TYPE_CODE)
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
                    ret_runtime_context->loaded_elf = true;
                }
                else if (vmr->type == SEL4UTILS_RES_TYPE_STACK)
                {
                    PD_CREATION_PRINT("Allocating stack (%zu pages)\n", vmr->region_pages);
                    mo_client_context_t mo = {0};
                    void *stack = sel4gpi_new_sized_stack(&vmr_rde, vmr->region_pages, &mo);

                    vmr->start = stack;
                    vmr->mo = mo;

                    GOTO_IF_ERR(stack == NULL, "failed to allocate a new stack");
                }
                else
                {
                    PD_CREATION_PRINT("Allocating VMR (%s) with %lu pages at %p\n",
                                      human_readable_va_res_type(vmr->type), vmr->region_pages, vmr->start);
                    mo_client_context_t mo = {0};
                    void *vmr_addr = sel4gpi_get_vmr(&vmr_rde, vmr->region_pages, vmr->start, vmr->type,
                                                     vmr->page_bits ? vmr->page_bits : MO_PAGE_BITS, &mo);
                    GOTO_IF_ERR(vmr_addr == NULL, "failed to allocate VMR (%s@%p)\n",
                                human_readable_va_res_type(vmr->type), vmr->start);

                    vmr->start = vmr_addr;
                    vmr->mo = mo;
                }

                break;
            default:
                GOTO_IF_COND(1, "Invalid sharing degree specified (%u) for VMR (%p)\n", vmr->share_mode, vmr->start);
                break;
            }

            // (XXX) Linh: if any of these were shallow copied, we won't have the underlying MO to later do
            //             any setup (e.g. TLS writing, CPU config), but it's likely an invalid config for
            //             these regions to be shallow-copied, and using separate CPU objects
            if (ret_runtime_context)
            {
                if (vmr->type == SEL4UTILS_RES_TYPE_IPC_BUF)
                {
                    ret_runtime_context->ipc_buf_cfg = vmr;
                }

                if (vmr->type == SEL4UTILS_RES_TYPE_STACK)
                {
                    ret_runtime_context->stack_cfg = vmr;
                }
            }
        }
    }

    if (ret_runtime_context)
    {
        ret_runtime_context->entry_point = cfg->entry_point == NULL ? auto_entry_point : cfg->entry_point;
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
            PD_CREATION_PRINT("Sharing RDE (type: %s, space ID: %u)\n", cap_type_to_str(rde->type), rde->space_id);
            error = pd_client_share_rde(&runnable->pd, rde->type, rde->space_id);
            WARN_IF_COND(error, "Couldn't share RDE (type: %u, space ID: %u)\n", rde->type, rde->space_id);
        }
    }

err_goto:
    return error;
}

static vmr_config_t *find_vmr_cfg_by_type(ads_config_t *cfg, sel4utils_reservation_type_t type)
{
    vmr_config_t *found = NULL;
    if (cfg->vmr_cfgs)
    {
        vmr_config_t *vmr = NULL;
        for (linked_list_node_t *curr = cfg->vmr_cfgs->head; curr != NULL; curr = curr->next)
        {
            vmr = (vmr_config_t *)curr->data;
            if (vmr->type == type)
            {
                found = vmr;
                break;
            }
        }
    }

    return found;
}

int sel4gpi_prepare_pd(pd_config_t *cfg, sel4gpi_runnable_t *runnable, int argc, seL4_Word *args)
{
    assert(cfg != NULL);
    assert(runnable != NULL);
    assert(argc == 0 || args != NULL);

    int error;

    GOTO_IF_COND(cfg == NULL || &runnable->pd == NULL, "Either no PD config given or PD to configure does not exist\n");

    seL4_CPtr free_slot;
    pd_client_context_t self_pd_conn = sel4gpi_get_pd_conn();

    seL4_Word cnode_guard = api_make_guard_skip_word(seL4_WordBits - PD_CSPACE_SIZE_BITS);

    mo_client_context_t ipc_mo = {0};
    runtime_context_t runtime_context = {0};

    error = sel4gpi_ads_configure(&cfg->ads_cfg, runnable, &cfg->osm_data_mo, &runtime_context);
    GOTO_IF_ERR(error, "Failed to configure ADS\n");
    assert(runtime_context.stack_cfg != NULL);
    assert(runtime_context.ipc_buf_cfg != NULL);

    error = rde_configure(cfg, runnable);
    GOTO_IF_ERR(error, "Failed to configure RDEs\n");

    // give the PD its CPU cap
    error = pd_client_send_core_cap(&runnable->pd, runnable->cpu.ep, NULL);
    GOTO_IF_ERR(error, "Failed to send CPU cap to PD\n");

    // give the PD its ADS cap
    error = pd_client_send_core_cap(&runnable->pd, runnable->ads.ep, NULL);
    GOTO_IF_ERR(error, "Failed to send ADS cap to PD\n");

    // give the PD its PD cap
    error = pd_client_send_core_cap(&runnable->pd, runnable->pd.ep, NULL);
    GOTO_IF_ERR(error, "Failed to send PD cap to PD\n");

    if (cfg->gpi_res_type_cfg)
    {
        for (linked_list_node_t *curr = cfg->gpi_res_type_cfg->head; curr != NULL; curr = curr->next)
        {
            gpi_cap_t type = (gpi_cap_t)curr->data;
            PD_CREATION_PRINT("Sharing %s Resources with PD\n", cap_type_to_str(type));
            error = pd_client_share_resource_by_type(&self_pd_conn, &runnable->pd, type);
            WARN_IF_COND(error, "Failed to share %s resources with PD\n", cap_type_to_str(type));
        }
    }

    if (cfg->fault_ep.ep == seL4_CapNull)
    {
        error = sel4gpi_alloc_endpoint(&cfg->fault_ep);
        GOTO_IF_COND(error || cfg->fault_ep.ep == seL4_CapNull,
                     "Couldn't allocate fault EP for PD\n");
        PD_CREATION_PRINT("Allocated new fault EP \n");
    }

    ep_client_context_t fault_ep_in_PD = {0};
    error = pd_client_send_core_cap(&runnable->pd,
                                    cfg->fault_ep.ep,
                                    &fault_ep_in_PD.ep);
    GOTO_IF_ERR(error, "Failed to send fault EP to PD\n");

    error = ep_client_get_raw_endpoint_in_PD(&runnable->pd, &cfg->fault_ep, &fault_ep_in_PD.raw_endpoint);
    GOTO_IF_ERR(error, "Failed to get raw EP in target PD's CSpace\n");
    PD_CREATION_PRINT("Sent fault EP to PD in slot 0x%lx\n", fault_ep_in_PD.raw_endpoint);

    void *init_stack_ptr = runtime_context.stack_cfg->start;
    void *entry_point = runtime_context.entry_point;
    seL4_Word *args_cp = args;

    if (!runtime_context.loaded_elf)
    {
        PD_CREATION_PRINT("C Runtime already initialized, setup the TLS ourselves\n");
        void *tp = NULL;
        error = setup_tls_in_stack(&runnable->ads,
                                   &runtime_context,
                                   &init_stack_ptr, &tp);
        GOTO_IF_ERR(error, "failed to write TLS\n");

        error = cpu_client_set_tls_base(&runnable->cpu, tp);
        GOTO_IF_ERR(error, "failed to set TLS base\n");

        entry_point = _start;

        // use the first argument for the user-given entry point,
        // since we're setting the PC to the generic `_start` entry
        args_cp = calloc(argc + 1, sizeof(seL4_Word));
        assert(args_cp != NULL);
        args_cp[0] = (seL4_Word)runtime_context.entry_point;
        for (int i = 0; i < argc; i++)
        {
            args_cp[i + 1] = args[i];
        }
        argc++;
    }

    error = pd_client_runtime_setup(&runnable->pd,
                                    &runnable->ads,
                                    &runnable->cpu,
                                    init_stack_ptr,
                                    argc,
                                    args_cp,
                                    entry_point,
                                    runtime_context.ipc_buf_cfg->start,
                                    runtime_context.osm_data);
    GOTO_IF_ERR(error, "failed to prepare runtime");

    PD_CREATION_PRINT("Configuring CPU Object, fault_ep: %lx entry: %p\n",
                      fault_ep_in_PD.raw_endpoint, entry_point);
    error = cpu_client_config(&runnable->cpu,
                              &runnable->ads,
                              &runnable->pd,
                              &runtime_context.ipc_buf_cfg->mo,
                              cnode_guard,
                              fault_ep_in_PD.raw_endpoint,
                              runtime_context.ipc_buf_cfg->start);
    GOTO_IF_ERR(error, "failed to configure CPU\n");

err_goto:
    // TODO cleanup things we've allocated
    if (args_cp && args_cp != args)
    {
        free(args_cp);
    }

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

void sel4gpi_generate_proc_config(pd_config_t *proc_cfg, const char *image_name, size_t stack_pages, size_t heap_pages)
{
    proc_cfg->ads_cfg.image_name = image_name;

    proc_cfg->ads_cfg.vmr_cfgs = linked_list_new();
    int n_cfgs = 0;
    vmr_config_t *heap_vmr = calloc(1, sizeof(vmr_config_t));
    assert(heap_vmr != NULL);
    heap_vmr->start = (void *)PD_HEAP_LOC;
    heap_vmr->type = SEL4UTILS_RES_TYPE_HEAP;
    heap_vmr->region_pages = heap_pages;
    heap_vmr->share_mode = GPI_DISJOINT;
    n_cfgs++;

    vmr_config_t *code_vmr = calloc(1, sizeof(vmr_config_t));
    assert(code_vmr != NULL);
    code_vmr->type = SEL4UTILS_RES_TYPE_CODE;
    code_vmr->share_mode = GPI_DISJOINT;
    n_cfgs++;

    vmr_config_t *stack_vmr = calloc(1, sizeof(vmr_config_t));
    assert(stack_vmr != NULL);
    stack_vmr->type = SEL4UTILS_RES_TYPE_STACK;
    stack_vmr->share_mode = GPI_DISJOINT;
    stack_vmr->region_pages = stack_pages;
    n_cfgs++;

    vmr_config_t *ipc_buf_vmr = calloc(1, sizeof(vmr_config_t));
    assert(ipc_buf_vmr != NULL);
    ipc_buf_vmr->type = SEL4UTILS_RES_TYPE_IPC_BUF;
    ipc_buf_vmr->share_mode = GPI_DISJOINT;
    ipc_buf_vmr->region_pages = 1;
    n_cfgs++;

    linked_list_insert_many(proc_cfg->ads_cfg.vmr_cfgs, n_cfgs, code_vmr, heap_vmr, stack_vmr, ipc_buf_vmr);

    proc_cfg->rde_cfg = linked_list_new();

    int n_rde_cfgs = 0;
    rde_config_t *mo_rde_cfg = calloc(1, sizeof(rde_config_t));
    assert(mo_rde_cfg != NULL);
    mo_rde_cfg->space_id = BADGE_SPACE_ID_NULL;
    mo_rde_cfg->type = GPICAP_TYPE_MO;
    n_rde_cfgs++;

    rde_config_t *resspc_rde_cfg = calloc(1, sizeof(rde_config_t));
    assert(resspc_rde_cfg != NULL);
    resspc_rde_cfg->space_id = BADGE_SPACE_ID_NULL;
    resspc_rde_cfg->type = GPICAP_TYPE_RESSPC;
    n_rde_cfgs++;

    linked_list_insert_many(proc_cfg->rde_cfg, n_rde_cfgs, mo_rde_cfg, resspc_rde_cfg);
}

void sel4gpi_config_pd_share_all_rdes(pd_config_t *cfg)
{
    osm_pd_shared_data_t *shared_data = ((osm_pd_shared_data_t *)sel4runtime_get_osm_shared_data());

    if (!cfg->rde_cfg)
    {
        cfg->rde_cfg = linked_list_new();
    }

    for (int i = GPICAP_TYPE_NONE + 1; i < GPICAP_TYPE_MAX; i++)
    {
        for (int j = 0; j < MAX_NS_PER_RDE; j++)
        {
            if (shared_data->rde[i][j].type.type != GPICAP_TYPE_NONE)
            {
                rde_config_t *rde = calloc(1, sizeof(rde_config_t));
                sel4gpi_add_rde_config(cfg, shared_data->rde[i][j].type.type, shared_data->rde[i][j].space_id);
                // linked_list_insert(cfg->rde_cfg, rde);
            }
        }
    }
}

void sel4gpi_generate_thread_config(pd_config_t *thread_cfg, void *thread_fn, ep_client_context_t *fault_ep)
{
    if (fault_ep)
    {
        thread_cfg->fault_ep = *fault_ep;
    }
    thread_cfg->ads_cfg.entry_point = thread_fn;
    thread_cfg->ads_cfg.vmr_cfgs = linked_list_new();

    int n_cfgs = 0;
    vmr_config_t *stack_cfg = calloc(1, sizeof(vmr_config_t));
    assert(stack_cfg != NULL);
    stack_cfg->type = SEL4UTILS_RES_TYPE_STACK;
    stack_cfg->share_mode = GPI_DISJOINT;
    stack_cfg->region_pages = DEFAULT_STACK_PAGES;
    n_cfgs++;

    vmr_config_t *ipc_buf_cfg = calloc(1, sizeof(vmr_config_t));
    assert(ipc_buf_cfg != NULL);
    ipc_buf_cfg->type = SEL4UTILS_RES_TYPE_IPC_BUF;
    ipc_buf_cfg->share_mode = GPI_DISJOINT;
    ipc_buf_cfg->region_pages = 1;
    n_cfgs++;

    linked_list_insert_many(thread_cfg->ads_cfg.vmr_cfgs, n_cfgs, stack_cfg, ipc_buf_cfg);

    // give the thread PD all of our current RDEs
    sel4gpi_config_pd_share_all_rdes(thread_cfg);

    // since we're sharing address spaces, transfer all MO caps to the thread PD
    sel4gpi_add_res_type_config(thread_cfg, GPICAP_TYPE_MO);
    // thread_cfg->gpi_res_type_cfg = linked_list_new();
    // linked_list_insert(thread_cfg->gpi_res_type_cfg, (void *)GPICAP_TYPE_MO);
}

void sel4gpi_config_destroy(pd_config_t *cfg)
{
    if (cfg->ads_cfg.vmr_cfgs)
    {
        linked_list_destroy(cfg->ads_cfg.vmr_cfgs, true);
    }

    if (cfg->rde_cfg)
    {
        linked_list_destroy(cfg->rde_cfg, true);
    }

    if (cfg->gpi_res_type_cfg)
    {
        linked_list_destroy(cfg->gpi_res_type_cfg, false);
    }

    free(cfg);
}

char *sel4gpi_share_degree_to_str(gpi_share_degree_t share_deg)
{
    switch (share_deg)
    {
    case GPI_SHARED:
        return "Shared";
    case GPI_DISJOINT:
        return "Disjoint";
    default:
        return "Invalid";
    }
}

void sel4gpi_add_rde_config(pd_config_t *cfg, gpi_cap_t rde_type, gpi_space_id_t space_id)
{
    if (cfg)
    {
        if (!cfg->rde_cfg)
        {
            cfg->rde_cfg = linked_list_new();
        }

        rde_config_t *new_rde_cfg = calloc(1, sizeof(rde_config_t));
        if (!new_rde_cfg)
        {
            UNCONDITIONAL_WARN("Malloc failed!\n");
            return;
        }

        new_rde_cfg->space_id = space_id;
        new_rde_cfg->type = rde_type;

        linked_list_insert(cfg->rde_cfg, new_rde_cfg);
    }
}

void sel4gpi_add_vmr_config(ads_config_t *cfg,
                            gpi_share_degree_t share_mode,
                            sel4utils_reservation_type_t type,
                            void *start,
                            void *dest_start,
                            uint64_t region_pages,
                            size_t page_bits,
                            mo_client_context_t *mo)
{
    if (cfg)
    {
        if (!cfg->vmr_cfgs)
        {
            cfg->vmr_cfgs = linked_list_new();
        }

        vmr_config_t *new_vmr_cfg = calloc(1, sizeof(vmr_config_t));
        assert(new_vmr_cfg != NULL);
        new_vmr_cfg->share_mode = share_mode;
        new_vmr_cfg->type = type;
        new_vmr_cfg->start = start;
        new_vmr_cfg->dest_start = dest_start;
        new_vmr_cfg->region_pages = region_pages;
        new_vmr_cfg->page_bits = page_bits;

        if (mo)
        {
            new_vmr_cfg->mo = *mo;
        }

        linked_list_insert(cfg->vmr_cfgs, new_vmr_cfg);
    }
}

void sel4gpi_add_res_type_config(pd_config_t *cfg, gpi_cap_t resource_type)
{
    if (cfg)
    {
        if (!cfg->gpi_res_type_cfg)
        {
            cfg->gpi_res_type_cfg = linked_list_new();
        }

        linked_list_insert(cfg->gpi_res_type_cfg, (void *)resource_type);
    }
}
