#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>

#include <vka/vka.h>
#include <vka/object.h>
#include <vka/capops.h>

#include <sel4gpi/pd_utils.h>
#include <sel4gpi/gpi_rpc.h>
#include <sel4gpi/resource_registry.h>
#include <sel4gpi/resource_server_utils.h>
#include <pb_print.h>

#if BENCHMARK_RESOURCE_SERVER
#include <sel4bench/arch/sel4bench.h>
#endif

/** @file
 * Utility functions for non-RT PDs that serve GPI resources
 */

// Generic buffer size for RPC messages, in bytes
// Must be larger than any RPC message in the system
// We could use the generated Message_size constants instead, if we wanted to be more precise
#define RPC_MSG_MAX_SIZE 256

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
                          void (*request_handler)(void *, void *, seL4_Word, seL4_CPtr, bool *),
                          int (*work_handler)(PdWorkReturnMessage *),
                          seL4_CPtr parent_ep,
                          gpi_space_id_t parent_pd_id,
                          int (*init_fn)(),
                          bool debug_print,
                          const pb_msgdesc_t *request_desc,
                          const pb_msgdesc_t *reply_desc)
{
    seL4_Error error;

    strncpy(context->resource_type_name, server_type, RESOURCE_TYPE_MAX_STRING_SIZE);
    context->request_handler = request_handler;
    context->work_handler = work_handler;
    context->mo_ep = sel4gpi_get_rde(GPICAP_TYPE_MO);
    context->resspc_ep = sel4gpi_get_rde(GPICAP_TYPE_RESSPC);
    context->vmr_rde = sel4gpi_get_bound_vmr_rde();
    context->pd_conn = sel4gpi_get_pd_conn();
    context->parent_pd_id = parent_pd_id;
    context->init_fn = init_fn;
    context->debug_print = debug_print;
    sel4gpi_rpc_env_init(&context->rpc_env, request_desc, reply_desc);

    context->parent_ep.ep = parent_ep;
    error = ep_client_get_raw_endpoint(&context->parent_ep);
    CHECK_ERROR(error, "Failed to retrieve parent EP\n");

    RESOURCE_SERVER_PRINTF("Resource server VMR_RDE: %lu\n", context->vmr_rde);
    RESOURCE_SERVER_PRINTF("Resource server PD_CAP: %lu\n", context->pd_conn.ep);
    RESOURCE_SERVER_PRINTF("Resource server MO ep: %lu\n", context->mo_ep);
    RESOURCE_SERVER_PRINTF("Resource server RESSPC ep: %lu\n", context->resspc_ep);

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

#if BENCHMARK_RESOURCE_SERVER
    sel4bench_init();
#endif

    // Create a default resource space
    error = resource_server_new_res_space(context, context->parent_pd_id, &context->default_space);
    CHECK_ERROR_GOTO(error, "failed to create resource server's default space", exit_main);
    RESOURCE_SERVER_PRINTF("Resource server's default space ID is 0x%u\n", context->default_space.id);

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
    RESOURCE_SERVER_PRINTF("Messaging parent process at slot %lu, sending space ID %u\n",
                           context->parent_ep.raw_endpoint, context->default_space.id);
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

#if BENCHMARK_RESOURCE_SERVER
        ccnt_t wait_start, wait_end;
        SEL4BENCH_READ_CCNT(wait_start);
#endif

        tag = resource_server_recv(context, &sender_badge);

#if BENCHMARK_RESOURCE_SERVER
        SEL4BENCH_READ_CCNT(wait_end);
#endif

        RESOURCE_SERVER_PRINTF("Received a message\n");

#if RESOURCE_SERVER_DEBUG
        char sender_badge_str[200];
        badge_sprint(sender_badge_str, sender_badge);
        char unwrapped_str[100];
        badge_sprint(unwrapped_str, seL4_GetBadge(0));

        RESOURCE_SERVER_PRINTF("Message on endpoint 0x%lx\n", seL4_GetCapPaddr(context->server_ep.raw_endpoint));
        RESOURCE_SERVER_PRINTF("- sender badge: %s\n", sender_badge_str);
        RESOURCE_SERVER_PRINTF("- extracaps %lu, capsunwrapped %lu\n",
                               seL4_MessageInfo_get_extraCaps(tag), seL4_MessageInfo_get_capsUnwrapped(tag));
#if CONFIG_DEBUG_BUILD
        RESOURCE_SERVER_PRINTF("- Received cap: type %u addr 0x%lx\n",
                               seL4_DebugCapIdentify(received_cap_path.capPtr),
                               seL4_GetCapPaddr(received_cap_path.capPtr));
#endif
        RESOURCE_SERVER_PRINTF("- unwrapped badge: %s\n", unwrapped_str);
#endif

        /* If the sender badge is NOTIF_BADGE, then we were woken up by the RT, and should check for work */
        if (sender_badge == NOTIF_BADGE)
        {
#if BENCHMARK_RESOURCE_SERVER
            printf("%s %s: Got work notif, waited for %lu\n", context->resource_type_name, SERVER_UTILS, wait_end - wait_start);
#endif

            /* Perform any pending work the RT requested */
            while (1)
            {
                RESOURCE_SERVER_PRINTF("Requesting work from root task\n");

                PdWorkReturnMessage work;
                error = pd_client_get_work(&context->pd_conn, &work);
                CHECK_ERROR_GOTO(error, "failed to get work from RT", exit_main);

                RESOURCE_SERVER_PRINTF("Got some work from RT\n");
                if (context->debug_print)
                {
                    printf("TEMPA it thinks debug print is on?\n");
                    pb_pretty_print(&PdWorkReturnMessage_msg, (void *)&work);
                }

                if (work.action != PdWorkAction_NO_WORK)
                {
                    context->work_handler(&work);
                }
                else
                {
                    RESOURCE_SERVER_PRINTF("No more work to be done\n");
                    break;
                }
            }

            continue;
        }

#if STORE_REPLY_CAP
        sel4gpi_store_reply_cap();
#endif

        /* Decode the message */
        char rpc_msg_buf[RPC_MSG_MAX_SIZE];
        char rpc_reply_buf[RPC_MSG_MAX_SIZE];

        error = sel4gpi_rpc_recv(&context->rpc_env, (void *)rpc_msg_buf);
        assert(error == 0);

        if (context->debug_print)
        {
            sel4gpi_rpc_print_request(&context->rpc_env, (void *)rpc_msg_buf);
        }

        /* Handle the message */
        context->request_handler(rpc_msg_buf,
                                 rpc_reply_buf,
                                 sender_badge,
                                 received_cap_path.capPtr,
                                 &need_new_receive_slot);

        /* Reply to message */
        seL4_MessageInfo_t reply_tag;
        error = sel4gpi_rpc_reply(&context->rpc_env, (void *)rpc_reply_buf, &reply_tag);
        assert(error == 0);
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

    mo_conn.ep = mo_cap;
    error = vmr_client_attach_no_reserve(context->vmr_rde,
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

    error = vmr_client_delete_by_vaddr(context->vmr_rde,
                                       vaddr);
    CHECK_ERROR(error, "failed to unattach from ADS");

    return error;
}

int resource_server_create_resource(resource_server_context_t *context,
                                    resspc_client_context_t *space_conn,
                                    gpi_obj_id_t resource_id)
{
    int error;

    RESOURCE_SERVER_PRINTF("Creating resource with ID 0x%u\n", resource_id);

    if (space_conn == NULL)
    {
        space_conn = &context->default_space;
    }

    error = resspc_client_create_resource(space_conn, resource_id);

    return error;
}

int resource_server_give_resource(resource_server_context_t *context,
                                  gpi_space_id_t space_id,
                                  gpi_obj_id_t resource_id,
                                  gpi_obj_id_t client_id,
                                  seL4_CPtr *dest)
{
    int error;

    RESOURCE_SERVER_PRINTF("Giving resource to client, resource ID 0x%u, client ID 0x%u\n", resource_id, client_id);

    error = pd_client_give_resource(&context->pd_conn,
                                    space_id,
                                    client_id,
                                    resource_id,
                                    dest);

    return error;
}

int resource_server_new_res_space(resource_server_context_t *context,
                                  gpi_obj_id_t client_id,
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

    RESOURCE_SERVER_PRINTF("Registered resource server, space ID is 0x%u\n", space_conn.id);

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
                                      mo->ep,
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

int resource_server_extraction_finish(resource_server_context_t *context, mo_client_context_t *mo,
                                      model_state_t *ms, int n_requests)
{
    int error = 0;

    clean_model_state(ms);

    /* Send the state to the RT */
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();
    error = pd_client_send_subgraph(&pd_conn, mo, true, n_requests);
    CHECK_ERROR_GOTO(error, "Failed to send subgraph\n", err_goto);

    /* Remove & destroy the MO */
    error = resource_server_unattach(context, (void *)ms);
    CHECK_ERROR_GOTO(error, "Failed to unattach MO for model extraction\n", err_goto);
    error = mo_component_client_disconnect(mo);
    CHECK_ERROR_GOTO(error, "Failed to delete MO for model extraction\n", err_goto);

err_goto:
    return error;
}

int resource_server_extraction_no_data(resource_server_context_t *context, int n_requests)
{
    int error = 0;

    /* Send the state to the RT */
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();
    error = pd_client_send_subgraph(&pd_conn, NULL, false, n_requests);

err_goto:
    return error;
}