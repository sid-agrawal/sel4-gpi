#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>

#include <vka/vka.h>
#include <vka/object.h>
#include <vka/capops.h>

#include <sel4gpi/pd_utils.h>
#include <sel4gpi/resource_server_utils.h>
#include <sel4gpi/resource_server_remote_utils.h>

/** @file
 * Utility functions for non-RT PDs that serve GPI resources
 */

#define CHECK_ERROR(error, msg)    \
    do                             \
    {                              \
        if (error != seL4_NoError) \
        {                          \
            ZF_LOGE("%s"           \
                    ", %d.",       \
                    msg,           \
                    error);        \
            return error;          \
        }                          \
    } while (0);

#define CHECK_ERROR_GOTO(check, msg, loc) \
    do                                    \
    {                                     \
        if ((check) != seL4_NoError)      \
        {                                 \
            ZF_LOGE(SERVER_UTILS ": %s"   \
                                 ", %d.", \
                    msg,                  \
                    check);               \
            error = -1;                   \
            goto loc;                     \
        }                                 \
    } while (0);

int resource_server_start(resource_server_context_t *context,
                          char *server_type,
                          seL4_MessageInfo_t (*request_handler)(seL4_MessageInfo_t, seL4_Word, seL4_CPtr, bool *),
                          int (*work_handler)(PdWorkReturnMessage *),
                          seL4_CPtr parent_ep,
                          uint64_t parent_pd_id,
                          int (*init_fn)())
{
    seL4_Error error;

    strncpy(context->resource_type_name, server_type, RESOURCE_TYPE_MAX_STRING_SIZE);
    context->request_handler = request_handler;
    context->work_handler = work_handler;
    context->mo_ep = sel4gpi_get_rde(GPICAP_TYPE_MO);
    context->resspc_ep = sel4gpi_get_rde(GPICAP_TYPE_RESSPC);
    context->ads_conn.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_space_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_VMR);
    context->pd_conn = sel4gpi_get_pd_conn();
    context->parent_pd_id = parent_pd_id;
    context->init_fn = init_fn;

    context->parent_ep.badged_server_ep_cspath.capPtr = parent_ep;
    error = ep_client_get_raw_endpoint(&context->parent_ep);
    CHECK_ERROR(error, "Failed to retrieve parent EP\n");

    printf("Resource server ADS_CAP: %ld\n", (seL4_Word)context->ads_conn.badged_server_ep_cspath.capPtr);
    printf("Resource server PD_CAP: %ld\n", (seL4_Word)context->pd_conn.badged_server_ep_cspath.capPtr);
    printf("Resource server MO ep: %ld\n", (seL4_Word)context->mo_ep);
    printf("Resource server RESSPC ep: %ld\n", (seL4_Word)context->resspc_ep);

    /* Allocate the Endpoint that the server will be listening on. */
    error = sel4gpi_alloc_endpoint(&context->server_ep);
    CHECK_ERROR(error, "Failed to allocate endpoint for resource server");
    RESOURCE_SERVER_PRINTF("Allocated server ep at %d\n", (int)context->server_ep.raw_endpoint);

    RESOURCE_SERVER_PRINTF("Going to main function\n");
    return resource_server_main((void *)context);
}

/**
 * Recv function for MCS or non-MCS kernel
 */
static seL4_MessageInfo_t resource_server_recv(resource_server_context_t *context,
                                               seL4_Word *sender_badge_ptr)
{
    /** NOTE:

     * the reply param of api_recv(third param) is only used in the MCS kernel.
     **/

    return api_recv(context->server_ep.raw_endpoint,
                    sender_badge_ptr,
                    context->mcs_reply);
}

/**
 * Reply function for MCS or non-MCS kernel
 */
static void resource_server_reply(resource_server_context_t *context,
                                  seL4_MessageInfo_t tag)
{
#if STORE_REPLY_CAP
    seL4_Send(sel4gpi_get_reply_cap(), tag);
#else
    api_reply(context->mcs_reply, tag);
#endif
}

static int resource_server_next_slot(resource_server_context_t *context,
                                     seL4_CPtr *slot)
{
    return pd_client_next_slot(&context->pd_conn, slot);
}

static int resource_server_free_slot(resource_server_context_t *context,
                                     seL4_CPtr slot)
{
    return pd_client_free_slot(&context->pd_conn, slot);
}

static int resource_server_clear_slot(resource_server_context_t *context,
                                      seL4_CPtr slot)
{
    return pd_client_clear_slot(&context->pd_conn, slot);
}

int resource_server_main(void *context_v)
{
    resource_server_context_t *context = (resource_server_context_t *)context_v;
    seL4_MessageInfo_t tag;
    seL4_Error error = 0;
    seL4_Word sender_badge;
    cspacepath_t received_cap_path;
    bool need_new_receive_slot;
    received_cap_path.root = PD_CAP_ROOT;
    received_cap_path.capDepth = PD_CAP_DEPTH;

    // Create a default resource space
    error = resource_server_new_res_space(context, context->parent_pd_id, &context->default_space);
    CHECK_ERROR_GOTO(error, "failed to create resource server's default space", exit_main);
    RESOURCE_SERVER_PRINTF("Resource server's default space ID is 0x%lx\n", context->default_space.id);

    // Perform any server-specific initialization
    if (context->init_fn != NULL)
    {
        RESOURCE_SERVER_PRINTF("Calling server's init function\n");

        error = context->init_fn();
        CHECK_ERROR_GOTO(error, "failed to initialize resource server", exit_main);
    }

    // Allocate the cap receive slot
    error = resource_server_next_slot(context, &received_cap_path.capPtr);
    CHECK_ERROR_GOTO(error, "failed to alloc cap receive slot", exit_main);

    seL4_SetCapReceivePath(
        received_cap_path.root,
        received_cap_path.capPtr,
        received_cap_path.capDepth);

    // Send our space ID to the parent process
    RESOURCE_SERVER_PRINTF("Messaging parent process at slot %d, sending space ID %d\n", (int)context->parent_ep.raw_endpoint, context->default_space.id);
    tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, context->default_space.id);
    seL4_Send(context->parent_ep.raw_endpoint, tag);

#if STORE_REPLY_CAP
    // Initialize the reply cap
    sel4gpi_clear_reply_cap();
#endif

    while (1)
    {
        /* Reset the cap receive path */
        seL4_SetCapReceivePath(
            received_cap_path.root,
            received_cap_path.capPtr,
            received_cap_path.capDepth);

        /* Receive a message */
        RESOURCE_SERVER_PRINTF("Ready to receive a message\n");
        tag = resource_server_recv(context, &sender_badge);

#if RESOURCE_SERVER_DEBUG
        char sender_badge_str[100];
        badge_sprint(sender_badge_str, sender_badge);
        char unwrapped_str[100];
        badge_sprint(unwrapped_str, seL4_GetBadge(0));

        RESOURCE_SERVER_PRINTF("Message on endpoint %p\n",  seL4_GetCapPaddr(context->server_ep.raw_endpoint));
        RESOURCE_SERVER_PRINTF("- sender badge: %s\n", sender_badge_str);
        RESOURCE_SERVER_PRINTF("- extracaps %d, capsunwrapped %d\n",
                               seL4_MessageInfo_get_extraCaps(tag), seL4_MessageInfo_get_capsUnwrapped(tag));
        RESOURCE_SERVER_PRINTF("- Received cap: type %d addr %p\n",
                               seL4_DebugCapIdentify(received_cap_path.capPtr),
                               seL4_GetCapPaddr(received_cap_path.capPtr));
        RESOURCE_SERVER_PRINTF("- unwrapped badge: %s\n", unwrapped_str);
#endif

        /* If the sender badge is NOTIF_BADGE, then we were woken up by the RT, and should check for work */
        if (sender_badge == NOTIF_BADGE)
        {
            /* Perform any pending work the RT requested */
            while (1)
            {
                PdWorkReturnMessage work;
                error = pd_client_get_work(&context->pd_conn, &work);
                CHECK_ERROR_GOTO(error, "failed to get work from RT", exit_main);

                if (work.action != PdWorkAction_NO_WORK)
                {
                    RESOURCE_SERVER_PRINTF("Got some work from RT (action: %d, space id: %d, object id: %d)\n",
                                           work.action,
                                           work.space_id,
                                           work.object_id);

                    context->work_handler(&work);
                }
                else
                {
                    // There is no more work to be done for now
                    break;
                }
            }

            continue;
        }

#if STORE_REPLY_CAP
        sel4gpi_store_reply_cap();
#endif

        /* Handle the message */
        seL4_MessageInfo_t reply_tag = context->request_handler(tag,
                                                                sender_badge,
                                                                received_cap_path.capPtr,
                                                                &need_new_receive_slot);

        /* Reply to message */
        resource_server_reply(context, reply_tag);

#if STORE_REPLY_CAP
        /* Clear the reply cap */
        sel4gpi_clear_reply_cap();
#endif

        /* Clear receive slot only if it was used */
        if (need_new_receive_slot)
        {
            RESOURCE_SERVER_PRINTF("Clearing cap receive slot\n");
            error = resource_server_clear_slot(context, received_cap_path.capPtr);
            CHECK_ERROR_GOTO(error, "failed to clear cap receive slot", exit_main);
        }
    }

exit_main:
    RESOURCE_SERVER_PRINTF("Suspending resource server");
    return -1;
}

/**
 * Attach a MO from a client request to the server's ADS
 * @param mo_cap The MO cap to attach
 * @param vaddr Returns the vaddr where MO was attached
 */
int resource_server_attach_mo(resource_server_context_t *context,
                              seL4_CPtr mo_cap,
                              void **vaddr)
{
    int error = 0;
    mo_client_context_t mo_conn;

    CHECK_ERROR(mo_cap == 0, "client did not attach MO for read/write op");

    mo_conn.badged_server_ep_cspath.capPtr = mo_cap;
    error = ads_client_attach(&context->ads_conn,
                              NULL,
                              &mo_conn,
                              SEL4UTILS_RES_TYPE_GENERIC,
                              vaddr);
    CHECK_ERROR(error, "failed to attach client's MO to ADS");

    return error;
}

/**
 * Remove a previously attached MO from the server's ADS
 * @param vaddr The vaddr where MO was attached
 */
int resource_server_unattach(resource_server_context_t *context,
                             void *vaddr)
{
    int error = 0;

    CHECK_ERROR(vaddr == NULL, "cannot unattach a NULL vaddr");

    error = ads_client_rm(&context->ads_conn,
                          vaddr);
    CHECK_ERROR(error, "failed to unattach from ADS");

    return error;
}

int resource_server_create_resource(resource_server_context_t *context,
                                    resspc_client_context_t *space_conn,
                                    uint64_t resource_id)
{
    int error;

    RESOURCE_SERVER_PRINTF("Creating resource with ID 0x%lx\n", resource_id);

    if (space_conn == NULL)
    {
        space_conn = &context->default_space;
    }

    error = resspc_client_create_resource(space_conn, resource_id);

    return error;
}

int resource_server_give_resource(resource_server_context_t *context,
                                  uint64_t space_id,
                                  uint64_t resource_id,
                                  uint64_t client_id,
                                  seL4_CPtr *dest)
{
    int error;

    RESOURCE_SERVER_PRINTF("Giving resource to client, resource ID 0x%lx, client ID 0x%lx\n", resource_id, client_id);

    error = pd_client_give_resource(&context->pd_conn,
                                    space_id,
                                    client_id,
                                    resource_id,
                                    dest);

    return error;
}

int resource_server_new_res_space(resource_server_context_t *context,
                                  uint64_t client_id,
                                  resspc_client_context_t *ret_conn)
{
    int error;

    RESOURCE_SERVER_PRINTF("Creating new NS\n");

    resspc_client_context_t space_conn;
    error = resspc_client_connect(context->resspc_ep, context->resource_type_name,
                                  &context->server_ep, client_id, &space_conn);
    CHECK_ERROR_GOTO(error, "failed to register resource space for server", err_goto);

    context->resource_type = space_conn.resource_type;
    *ret_conn = space_conn;

    RESOURCE_SERVER_PRINTF("Registered resource server, space ID is 0x%lx\n", space_conn.id);

err_goto:
    return error;
}

int resource_server_extraction_setup(resource_server_context_t *context,
                                     int n_pages,
                                     mo_client_context_t *mo,
                                     model_state_t **ms)
{
    int error = 0;

    // Allocate an MO for the extraction
    size_t mem_size = SIZE_BITS_TO_BYTES(MO_PAGE_BITS) * n_pages;
    error = mo_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_MO), n_pages, MO_PAGE_BITS, mo);
    CHECK_ERROR_GOTO(error, "failed to allocate MO for model extraction", err_goto);

    void *mem_vaddr;
    error = resource_server_attach_mo(context,
                                      mo->badged_server_ep_cspath.capPtr,
                                      &mem_vaddr);
    CHECK_ERROR_GOTO(error, "failed to attach MO for model extraction", err_goto);

    // Initialize model state
    *ms = (model_state_t *)mem_vaddr;
    void *free_mem = mem_vaddr + sizeof(model_state_t);
    size_t free_size = mem_size - sizeof(model_state_t);
    init_model_state(*ms, free_mem, free_size);

err_goto:
    return error;
}

int resource_server_extraction_finish(resource_server_context_t *context, mo_client_context_t *mo, model_state_t *ms)
{
    int error = 0;

    clean_model_state(ms);

    /* Send the state to the RT */
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();
    error = pd_client_send_subgraph(&pd_conn, mo, true);
    CHECK_ERROR_GOTO(error, "Failed to send subgraph\n", err_goto);

    /* Remove & destroy the MO */
    error = resource_server_unattach(context, (void *)ms);
    CHECK_ERROR_GOTO(error, "Failed to unattach MO for model extraction\n", err_goto);
    error = mo_component_client_disconnect(mo);
    CHECK_ERROR_GOTO(error, "Failed to delete MO for model extraction\n", err_goto);

err_goto:
    return error;
}

int resource_server_extraction_no_data(resource_server_context_t *context)
{
    int error = 0;

    /* Send the state to the RT */
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();
    error = pd_client_send_subgraph(&pd_conn, NULL, false);

err_goto:
    return error;
}