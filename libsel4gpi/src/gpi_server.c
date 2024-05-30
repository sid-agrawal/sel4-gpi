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
#include <vka/vka.h>
#include <vka/object.h>
#include <vka/object_capops.h>

#include <sel4gpi/gpi_server.h>
#include <sel4gpi/ads_component.h>
#include <sel4gpi/cpu_component.h>
#include <sel4gpi/resource_space_component.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>

// Defined for utility printing macros
#define DEBUG_ID GPI_DEBUG
#define SERVER_ID GPISERVS

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
                               seL4_CPtr *server_ep_cap)
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

    /* Get a CPtr to the parent's root cnode. */
    vka_cspace_make_path(parent_vka, 0, &parent_cspace_cspath);

    get_gpi_server()->server_simple = parent_simple;
    get_gpi_server()->server_vka = parent_vka;
    get_gpi_server()->server_cspace = parent_cspace_cspath.root;
    get_gpi_server()->server_vspace = parent_vspace;
    get_gpi_server()->rt_pd_id = RT_PD_ID;

    /* Allocate the Endpoint that the server will be listening on. */
    error = vka_alloc_endpoint(parent_vka, &get_gpi_server()->server_ep_obj);
    if (error != 0)
    {
        ZF_LOGE(GPISERVP "spawn_thread: failed to alloc endpoint, err=%d.",
                error);
        return error;
    }

    *server_ep_cap = get_gpi_server()->server_ep_obj.cptr;

    /* Setup the Resource Space Component */
    // This must be initialized first so that the other components can make their own resource spaces
    resspc_component_initialize(parent_simple, parent_vka, parent_cspace_cspath.root, parent_vspace,
                                get_gpi_server()->server_thread, get_gpi_server()->server_ep_obj);

    /* Setup the PD Component */
    pd_component_initialize(parent_simple, parent_vka, parent_cspace_cspath.root, parent_vspace,
                            get_gpi_server()->server_thread, get_gpi_server()->server_ep_obj);

    /* Setup the ADS Component */
    ads_component_initialize(parent_simple, parent_vka, parent_cspace_cspath.root, parent_vspace,
                             get_gpi_server()->server_thread, get_gpi_server()->server_ep_obj);

    /* Setup MO Component */
    mo_component_initialize(parent_simple, parent_vka, parent_cspace_cspath.root, parent_vspace,
                            get_gpi_server()->server_thread, get_gpi_server()->server_ep_obj);

    /* Setup the CPU Component */
    cpu_component_initialize(parent_simple, parent_vka, parent_cspace_cspath.root, parent_vspace,
                             get_gpi_server()->server_thread, get_gpi_server()->server_ep_obj);

    /* Initialize the root task's PD resource */
    forge_pd_for_root_task(get_gpi_server()->rt_pd_id);

    /* Initialize the root task's ADS resource */
    seL4_CPtr ads_cap;
    forge_ads_cap_from_vspace(get_gpi_server()->server_vspace, get_gpi_server()->server_vka,
                              get_gpi_server()->rt_pd_id, &ads_cap, &get_gpi_server()->rt_ads_id);

    /* Initialize the resource types */
    resource_types_initialize();

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
                         "with %d.",
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
                         "%d.",
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
