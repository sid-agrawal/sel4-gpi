/**
 * @file gpi_server.c
 * @author Sid Agrawal(sid@sid-agrawal.c)
 * @brief Implements functions needed by a parent to interact with the gpi server.
 * @version 0.1
 * @date 2022-04-05
 *
 * @copyright Copyright (c) 2022
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>

#include <sel4/sel4.h>
#include <sel4utils/strerror.h>
#include <sel4platsupport/device.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vka/object_capops.h>

#include <sel4gpi/gpi_server.h>
#include <sel4gpi/ads_component.h>
#include <sel4gpi/cpu_component.h>
#include <sel4gpi/resource_space_component.h>
#include <sel4gpi/endpoint_component.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/error_handle.h>

// Defined for utility printing macros
#define DEBUG_ID GPI_DEBUG
#define SERVER_ID GPISERVS
#define DEFAULT_ERR 1

// Default ID of the root task's PD
#define RT_PD_ID 0

static gpi_server_context_t gpi_server;

gpi_server_context_t *get_gpi_server(void)
{
    return &gpi_server;
}

static inline seL4_MessageInfo_t recv(seL4_Word *sender_badge_ptr)
{
    /** NOTE:

     * the reply param of api_recv(third param) is only used in the MCS kernel.
     **/

    return api_recv(get_gpi_server()->server_ep_obj.cptr,
                    sender_badge_ptr,
                    get_gpi_server()->server_thread.reply.cptr);
}

static inline void reply(seL4_MessageInfo_t tag)
{
    api_reply(get_gpi_server()->server_thread.reply.cptr, tag);
}

seL4_Error
gpi_server_parent_spawn_thread(simple_t *parent_simple, vka_t *parent_vka,
                               vspace_t *parent_vspace,
                               uint8_t priority,
                               seL4_CPtr *server_ep_cap,
                               sync_mutex_t *mx,
                               int *num_gen_irqs,
                               sel4ps_irq_t *gen_irqs)
{
    seL4_Error error;
    cspacepath_t parent_cspace_cspath;
    seL4_MessageInfo_t tag;

    if (parent_simple == NULL || parent_vka == NULL || parent_vspace == NULL)
    {
        return seL4_InvalidArgument;
    }

    /* This will clear out all the fields in osm_caps too*/
    memset(get_gpi_server(), 0, sizeof(gpi_server_context_t));

    get_gpi_server()->is_root = true;

    /* Get a CPtr to the parent's root cnode. */
    vka_cspace_make_path(parent_vka, 0, &parent_cspace_cspath);

    get_gpi_server()->server_simple = parent_simple;
    get_gpi_server()->server_vka = parent_vka;
    get_gpi_server()->server_cspace = parent_cspace_cspath.root;
    get_gpi_server()->server_vspace = parent_vspace;
    get_gpi_server()->rt_pd_id = RT_PD_ID;
    get_gpi_server()->mx = mx;
    get_gpi_server()->num_gen_irqs = num_gen_irqs;
    get_gpi_server()->gen_irqs = gen_irqs;

    /* Allocate the Endpoint that the server will be listening on. */
    error = vka_alloc_endpoint(parent_vka, &get_gpi_server()->server_ep_obj);
    if (error != 0)
    {
        ZF_LOGE(GPISERVP "spawn_thread: failed to alloc endpoint, err=%u.",
                error);
        return error;
    }

    *server_ep_cap = get_gpi_server()->server_ep_obj.cptr;

    /* Initialize the resource types */
    // resspc component initialization requires that the types are initialized first
    resource_types_initialize();

    /* Setup the Resource Space Component */
    // This component must be initialized first so that the other components can make their own resource spaces
    resspc_component_initialize(parent_vka, parent_vspace, get_gpi_server()->server_ep_obj);

    /* Setup the PD Component */
    pd_component_initialize(parent_vka, parent_vspace, get_gpi_server()->server_ep_obj);

    /* Setup the ADS Component */
    ads_component_initialize(parent_vka, parent_vspace, get_gpi_server()->server_ep_obj);

    /* Setup MO Component */
    mo_component_initialize(parent_vka, parent_vspace, get_gpi_server()->server_ep_obj);

    /* Setup the CPU Component */
    cpu_component_initialize(parent_vka, parent_vspace, get_gpi_server()->server_ep_obj);

    /* Setup the Endpoint component */
    ep_component_initialize(parent_vka, parent_vspace, get_gpi_server()->server_ep_obj);

    /* Initialize the root task's PD resource */
    forge_pd_for_root_task(get_gpi_server()->rt_pd_id);

    /* Initialize the root task's ADS resource */
    seL4_CPtr ads_cap;
    forge_ads_cap_from_vspace(get_gpi_server()->server_vspace, get_gpi_server()->server_vka,
                              get_gpi_server()->rt_pd_id, &ads_cap, &get_gpi_server()->rt_ads_id);

    /* And also allocate a badged copy of the Server's endpoint that the Parent
     * can use to send to the Server. This is used to allow the Server to report
     * back to the Parent on whether or not the Server successfully bound to a
     * platform serial driver.
     *
     * This badged endpoint will be reused by the library as the Parent's badged
     * Endpoint cap, if the Parent itself ever chooses to connect() to the
     * Server later on.
     */

    get_gpi_server()->parent_badge_value = GPI_SERVER_BADGE_PARENT_VALUE;

    error = vka_mint_object(parent_vka, &get_gpi_server()->server_ep_obj,
                            &get_gpi_server()->_badged_server_ep_cspath,
                            seL4_AllRights,
                            get_gpi_server()->parent_badge_value);
    if (error != 0)
    {
        ZF_LOGE(GPISERVP "spawn_thread: Failed to mint badged Endpoint cap to "
                         "server.\n"
                         "\tParent cannot confirm Server thread successfully spawned.");
        goto out;
    }

    sel4utils_thread_config_t config = thread_config_default(parent_simple,
                                                             parent_cspace_cspath.root,
                                                             seL4_NilData,
                                                             get_gpi_server()->server_ep_obj.cptr,
                                                             priority);
    error = sel4utils_configure_thread_config(parent_vka,
                                              parent_vspace,
                                              parent_vspace,
                                              config,
                                              &get_gpi_server()->server_thread);
    if (error != 0)
    {
        ZF_LOGE(GPISERVP "spawn_thread: sel4utils_configure_thread failed "
                         "with %u.",
                error);
        goto out;
    }

    NAME_THREAD(get_gpi_server()->server_thread.tcb.cptr, "gpi server");
    error = sel4utils_start_thread(&get_gpi_server()->server_thread,
                                   (sel4utils_thread_entry_fn)&gpi_server_main,
                                   NULL, NULL, 1);
    if (error != 0)
    {
        ZF_LOGE(GPISERVP "spawn_thread: sel4utils_start_thread failed with "
                         "%u.",
                error);
        goto out;
    }

    /* When the Server is spawned, it will reply to tell us whether or not it
     * successfully bound itself to the platform serial device. Block here
     * and wait for that reply.
     */
    tag = seL4_MessageInfo_new(0, 0, 0, 1);
    tag = seL4_Call(get_gpi_server()->_badged_server_ep_cspath.capPtr, tag);

    /* Did all go well with the server? */
    error = seL4_MessageInfo_get_label(tag);
    if (error != 0)
    {
        ZF_LOGE(GPISERVP "spawn_thread: Server thread failed to bind to the "
                         "platform serial device.");
        goto out;
    }

    OSDB_PRINTF("spawn_thread: Server thread binded well. at public EP %lu\n",
                get_gpi_server()->server_ep_obj.cptr);
    return 0;

out:
    OSDB_PRINTF("spawn_thread: Server ran into an error.\n");
    if (get_gpi_server()->_badged_server_ep_cspath.capPtr != 0)
    {
        vka_cspace_free_path(parent_vka, get_gpi_server()->_badged_server_ep_cspath);
    }

    vka_free_object(parent_vka, &get_gpi_server()->server_ep_obj);
    return error;
}

/**
 * @brief The starting point for the gpi server's thread.
 *
 */
void gpi_server_main()
{
    int error = 0;
    seL4_MessageInfo_t tag;
    cspacepath_t received_cap_path;

#if BENCHMARK_GPI_SERVER
    sel4bench_init();
#endif

    /* The Parent will seL4_Call() to us, the Server, right after spawning us.
     * It will expect us to seL4_Reply() with an error status code - we will
     * send this Reply.
     *
     * First call seL4_Recv() to get the Reply cap back to the Parent, and then
     * seL4_Reply to report our status.
     */
    seL4_Word sender_badge;
    recv(&sender_badge);
    assert(sender_badge == GPI_SERVER_BADGE_PARENT_VALUE);

    tag = seL4_MessageInfo_new(0, 0, 0, 1);
    reply(tag);

    /* If the bind failed, this thread has essentially failed its mandate, so
     * there is no reason to leave it scheduled. Kill it (to whatever extent
     * that is possible).
     */

    OSDB_PRINTF("gpi_server_main: Got a call from the parent.\n");
    if (error != 0)
    {
        seL4_TCB_Suspend(get_gpi_server()->server_thread.tcb.cptr);
    }

    // Allocate an initial receive path
    error = vka_cspace_alloc_path(get_gpi_server()->server_vka, &received_cap_path);
    assert(error == 0);
    OSDB_PRINTF("main: Entering main loop and accepting requests.\n");

    while (1)
    {
        // The resource components allocate a new receive path if necessary
        seL4_SetCapReceivePath(
            /* _service */ received_cap_path.root,
            /* index */ received_cap_path.capPtr,
            /* depth */ received_cap_path.capDepth);
        tag = recv(&sender_badge);

        OSDB_PRINTF("Got message on EP with ");
        BADGE_PRINT(sender_badge);

        gpi_cap_t cap_type = get_cap_type_from_badge(sender_badge);

        resource_component_context_t *component;
        switch (cap_type)
        {
        case GPICAP_TYPE_ADS:
            component = &get_gpi_server()->ads_component;
            break;
        case GPICAP_TYPE_VMR:
            component = &get_gpi_server()->ads_component;
            break;
        case GPICAP_TYPE_MO:
            component = &get_gpi_server()->mo_component;
            break;
        case GPICAP_TYPE_CPU:
            component = &get_gpi_server()->cpu_component;
            break;
        case GPICAP_TYPE_PD:
            component = &get_gpi_server()->pd_component;
            break;
        case GPICAP_TYPE_RESSPC:
            component = &get_gpi_server()->resspc_component;
            break;
        case GPICAP_TYPE_EP:
            component = &get_gpi_server()->ep_component;
            break;
        default:
            gpi_panic("gpi_server_main: Unknown cap type.", cap_type);
            break;
        }

        resource_component_handle(component,
                                  tag,
                                  sender_badge,
                                  &received_cap_path);
    }

    // serial_server_func_kill();
    /* After we break out of the loop, seL4_TCB_Suspend ourselves */
    ZF_LOGI(GPISERVS "main: Suspending.");
    seL4_TCB_Suspend(get_gpi_server()->server_thread.tcb.cptr);
}

void gpi_panic(char *reason, uint64_t code)
{
    printf("PANIC: %s. CODE: %ld\n", reason, code);
    assert(0);
}

void gpi_debug_print_resources(void)
{
    printf("\n\n*** LISTING REMAINING RESOURCES ***\n");
    resource_component_debug_print(get_pd_component());
    resource_component_debug_print(get_ads_component());
    resource_component_debug_print(get_cpu_component());
    resource_component_debug_print(get_mo_component());
    resource_component_debug_print(get_ep_component());
    printf("*** DONE LISTING RESOURCES ***\n\n");

    printf("Expected resources to remain:\n");
    printf(" - RT PD (%u)\n", get_gpi_server()->rt_pd_id);
    printf(" - RT ADS (%u)\n", get_gpi_server()->rt_ads_id);

    ads_component_registry_entry_t *ads_entry = (ads_component_registry_entry_t *)
        resource_component_registry_get_by_id(get_ads_component(), get_gpi_server()->rt_ads_id);

    for (resource_registry_node_t *node = ads_entry->ads.attach_registry.head; node != NULL; node = node->hh.next)
    {
        attach_node_t *attach_node = (attach_node_t *)node;
        printf(" - RT MO (%u)\n", (int)attach_node->mo_id);
    }
    printf("\n\n");
}

static seL4_CPtr find_irq_handler(sel4ps_irq_t *irqs, int *num_irqs, int irq)
{
    for (int i = 0; i < *num_irqs; i++)
    {
        bool found = false;
        switch (irqs[i].irq.type)
        {
        case PS_INTERRUPT:
            if (irqs[i].irq.irq.number == irq)
            {
                found = true;
            }
            break;
        case PS_TRIGGER:
            if (irqs[i].irq.trigger.number == irq)
            {
                found = true;
            }
        default:
            /* we currently don't store any other types of IRQs */
            break;
        }

        if (found)
        {
            return irqs[i].handler_path.capPtr;
        }
    }

    return seL4_CapNull;
}

seL4_CPtr gpi_get_irq_handler(vka_t *vka, simple_t *simple, sel4ps_irq_t *irqs, int *num_irqs, int irq)
{
    assert(irqs != NULL);
    assert(num_irqs != NULL);

    seL4_CPtr handler_slot = find_irq_handler(irqs, num_irqs, irq);
    if (handler_slot == seL4_CapNull)
    {
        cspacepath_t handler_path;
        ps_irq_t irq_info = {0};
#ifdef CONFIG_PLAT_ODROIDC4
        if (irq == SERIAL_IRQ)
        {
            irq_info.type = PS_TRIGGER;
            irq_info.trigger.number = irq;
            irq_info.trigger.trigger = PS_EDGE_TRIGGERED;
        }
#else
        irq_info.irq.number = irq;
        irq_info.type = PS_INTERRUPT;
#endif

        int error = sel4platsupport_copy_irq_cap(vka, simple, &irq_info, &handler_path);
        if (error)
        {
            OSDB_PRINTERR("Failed to get IRQ %d Handler\n", irq);
            return seL4_CapNull;
        }

        irqs[*num_irqs].irq = irq_info;
        irqs[*num_irqs].handler_path = handler_path;
        (*num_irqs)++;

        handler_slot = handler_path.capPtr;
    }

    return handler_slot;
}
