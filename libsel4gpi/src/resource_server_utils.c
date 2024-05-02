#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/resource_server_utils.h>

#define SERVER_UTILS "SERVER_UTILS"

// Could use the server's debug function instead
#if RESOURCE_SERVER_DEBUG
#define RESOURCE_SERVER_PRINTF(...)  \
    do                               \
    {                                \
        printf("%s ", SERVER_UTILS); \
        printf(__VA_ARGS__);         \
    } while (0);
#else
#define RESOURCE_SERVER_PRINTF(...)
#endif

#define CHECK_ERROR(error, msg)    \
    do                             \
    {                              \
        if (error != seL4_NoError) \
        {                          \
            ZF_LOGE(SERVER_UTILS   \
                    "%s: %s"       \
                    ", %d.",       \
                    __func__,      \
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
            ZF_LOGE(SERVER_UTILS "%s"     \
                                 ", %d.", \
                    msg,                  \
                    check);               \
            error = -1;                   \
            goto loc;                     \
        }                                 \
    } while (0);

int start_resource_server_pd(uint64_t rde_id,
                             seL4_CPtr rde_pd_cap,
                             char *image_name,
                             seL4_CPtr *server_pd_cap,
                             uint64_t *resource_manager_id)
{
    int error;

    // Current pd
    pd_client_context_t current_pd;
    current_pd.badged_server_ep_cspath.capPtr = sel4gpi_get_pd_cap();

    // Create a temporary endpoint for the parent to listen on
    seL4_CPtr ep;
    error = pd_client_alloc_ep(&current_pd, &ep);
    CHECK_ERROR(error, "failed to allocate endpoint");

    // Create a new PD
    pd_client_context_t new_pd;
    seL4_CPtr free_slot;
    error = pd_client_next_slot(&current_pd, &free_slot);
    CHECK_ERROR(error, "failed to allocate slot");
    error = pd_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_PD), free_slot, &new_pd);
    CHECK_ERROR(error, "failed to create new pd");

    if (server_pd_cap)
    {
        *server_pd_cap = new_pd.badged_server_ep_cspath.capPtr;
    }

    // Create a new ADS Cap, which will be in the context of a PD and image
    ads_client_context_t new_ads;
    error = pd_client_next_slot(&current_pd, &free_slot);
    CHECK_ERROR(error, "failed to allocate slot");
    error = ads_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_ADS), free_slot, &new_ads, NULL);
    CHECK_ERROR(error, "failed to create new ads");

    error = pd_client_next_slot(&current_pd, &free_slot);
    CHECK_ERROR(error, "failed to allocate slot");
    cpu_client_context_t new_cpu;
    error = cpu_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_CPU), free_slot, &new_cpu);
    CHECK_ERROR(error, "failed to create new cpu");

    // Make a new AS, loads an image
    error = pd_client_load(&new_pd, &new_ads, &new_cpu, image_name);
    CHECK_ERROR(error, "failed to load pd image");

    // Copy the parent ep to the new PD
    seL4_Word parent_ep_slot;
    error = pd_client_send_cap(&new_pd, ep, &parent_ep_slot);
    CHECK_ERROR(error, "failed to send parent's ep cap to pd");

    // Share the MO RDE (requires that the current process has one)
    error = pd_client_share_rde(&new_pd, GPICAP_TYPE_MO, NSID_DEFAULT);
    CHECK_ERROR(error, "failed to share parent's MO RDE with pd");

    // Copy the RDE to the new PD
    if (rde_pd_cap > 0)
    {
        RESOURCE_SERVER_PRINTF("SENDING RDE\n");
        error = pd_client_add_rde(&new_pd, rde_pd_cap, rde_id, NSID_DEFAULT);
        CHECK_ERROR(error, "failed to send rde to pd");
    }

    // Setup the args
    int argc = 1;
    seL4_Word args[argc];
    args[0] = parent_ep_slot;

    // Start it
    error = pd_client_start(&new_pd, argc, args);
    CHECK_ERROR(error, "failed to start pd");

    // Wait for it to finish starting
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(ep, NULL);
    int n_caps = seL4_MessageInfo_get_extraCaps(tag);
    error = seL4_MessageInfo_get_label(tag);
    CHECK_ERROR(error, "message from server is a failure");

    if (resource_manager_id)
    {
        *resource_manager_id = seL4_GetMR(0);
    }

    // Cleanup temporary endpoint
    // (XXX) Arya: why does this free cause future allocs to break?
    // vka_free_object(vka, &ep_object);

    return 0;
}

int resource_server_start(resource_server_context_t *context,
                          gpi_cap_t server_type,
                          seL4_MessageInfo_t (*request_handler)(seL4_MessageInfo_t, seL4_Word, seL4_CPtr, bool *),
                          seL4_CPtr parent_ep,
                          int (*init_fn)())
{
    seL4_Error error;

    context->resource_type = server_type;
    context->request_handler = request_handler;
    context->mo_ep = sel4gpi_get_rde(GPICAP_TYPE_MO);
    context->ads_conn.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_ns_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_ADS);
    context->pd_conn.badged_server_ep_cspath.capPtr = sel4gpi_get_pd_cap();
    context->parent_ep = parent_ep;
    context->init_fn = init_fn;

    printf("Ramdisk: ADS_CAP: %ld\n", (seL4_Word)context->ads_conn.badged_server_ep_cspath.capPtr);
    printf("Ramdisk: PD_CAP: %ld\n", (seL4_Word)context->pd_conn.badged_server_ep_cspath.capPtr);
    printf("Ramdisk: MO ep: %ld\n", (seL4_Word)context->mo_ep);

    /* Allocate the Endpoint that the server will be listening on. */
    error = pd_client_alloc_ep(&context->pd_conn, &context->server_ep);
    CHECK_ERROR(error, "Failed to allocate endpoint for resource server");
    RESOURCE_SERVER_PRINTF("Allocated server ep at %d\n", (int)context->server_ep);

    RESOURCE_SERVER_PRINTF("Going to main function\n");
    return resource_server_main((void *)context);
}

seL4_MessageInfo_t resource_server_recv(resource_server_context_t *context,
                                        seL4_Word *sender_badge_ptr)
{
    /** NOTE:

     * the reply param of api_recv(third param) is only used in the MCS kernel.
     **/

    return api_recv(context->server_ep,
                    sender_badge_ptr,
                    context->mcs_reply);
}

void resource_server_reply(resource_server_context_t *context,
                           seL4_MessageInfo_t tag)
{
    api_reply(context->mcs_reply, tag);
}

int resource_server_next_slot(resource_server_context_t *context,
                              seL4_CPtr *slot)
{
    return pd_client_next_slot(&context->pd_conn, slot);
}

int resource_server_free_slot(resource_server_context_t *context,
                              seL4_CPtr slot)
{
    return pd_client_free_slot(&context->pd_conn, slot);
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

    // Register the resource server with the PD component
    RESOURCE_SERVER_PRINTF("Registering the resource server with the PD component\n");
    error = pd_client_register_resource_manager(&context->pd_conn, context->resource_type, context->server_ep, &context->server_id);
    CHECK_ERROR_GOTO(error, "failed to register resource server", exit_main);
    RESOURCE_SERVER_PRINTF("Registered resource server, ID is 0x%lx\n", context->server_id);

    // Perform any server-specific initialization
    if (context->init_fn != NULL)
    {
        RESOURCE_SERVER_PRINTF("Calling server's init function\n");

        error = context->init_fn();
        CHECK_ERROR_GOTO(error, "failed to initialize resource server", exit_main);
    }

    // Allocate the first cap receive slot
    error = resource_server_next_slot(context, &received_cap_path.capPtr);
    CHECK_ERROR_GOTO(error, "failed to alloc cap receive slot", exit_main);

    seL4_SetCapReceivePath(
        received_cap_path.root,
        received_cap_path.capPtr,
        received_cap_path.capDepth);

    // Send our ep to the parent process
    RESOURCE_SERVER_PRINTF("Messaging parent process at slot %d, sending ID %d\n", (int)context->parent_ep, context->server_id);
    tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, context->server_id);
    seL4_Send(context->parent_ep, tag);

    while (1)
    {
        /* Receive a message */
        RESOURCE_SERVER_PRINTF("Ready to receive a message\n");
        tag = resource_server_recv(context, &sender_badge);
        int op = seL4_GetMR(RSMSGREG_FUNC);
        RESOURCE_SERVER_PRINTF("Received message, op is %d, passing to request handler\n", op);
        seL4_MessageInfo_t reply_tag = context->request_handler(tag, sender_badge, received_cap_path.capPtr, &need_new_receive_slot);

        /**
         * Free slot and reallocate only if needed
         * Some requests come from the root task, so we cannot message it
         * */
        if (need_new_receive_slot)
        {
            // We need to finish setting up for the next request before responding to this one
            // This is because the next request could be GET_RR, in which case we cannot
            // be requesting a new slot after it is sent

            // Save state of message registers for reply
            seL4_Word arg0 = seL4_GetMR(0);
            seL4_Word arg1 = seL4_GetMR(1);

            RESOURCE_SERVER_PRINTF("Freeing cap receive slot\n");
            error = resource_server_free_slot(context, received_cap_path.capPtr);
            CHECK_ERROR_GOTO(error, "failed to free cap receive slot", exit_main);

            // (XXX) Arya: Do we need to reallocate if we just delete the contents?
            RESOURCE_SERVER_PRINTF("Allocating new cap receive slot\n");
            error = resource_server_next_slot(context, &received_cap_path.capPtr);
            CHECK_ERROR_GOTO(error, "failed to alloc cap receive slot", exit_main);

            seL4_SetCapReceivePath(
                received_cap_path.root,
                received_cap_path.capPtr,
                received_cap_path.capDepth);

            // Restore state of message registers for reply
            seL4_SetMR(0, arg0);
            seL4_SetMR(1, arg1);
        }

        /* Reply to message */
        resource_server_reply(context, reply_tag);
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
    // (XXX) Arya: Track the MO so we can unattach and free it later
    int error = 0;
    mo_client_context_t mo_conn;

    CHECK_ERROR(mo_cap == 0, "client did not attach MO for read/write op");

    mo_conn.badged_server_ep_cspath.capPtr = mo_cap;
    error = ads_client_attach(&context->ads_conn,
                              NULL,
                              &mo_conn,
                              vaddr);
    CHECK_ERROR(error, "failed to attach client's MO to ADS");

    return error;
}

int resource_server_get_rr(seL4_CPtr server_ep,
                           seL4_Word res_id,
                           seL4_Word pd_id,
                           seL4_Word server_pd_id,
                           void *remote_vaddr,
                           void *local_vaddr,
                           size_t size,
                           model_state_t **ret_state)
{
    RESOURCE_SERVER_PRINTF("requesting resource relations for ID 0x%lx\n", res_id);
    RESOURCE_SERVER_PRINTF("Shared mem local addr: %p, remote addr: %p\n", local_vaddr, remote_vaddr);

    // Send IPC to resource server
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, RSMSGREG_EXTRACT_RR_REQ_END);
    seL4_SetMR(RSMSGREG_FUNC, RS_FUNC_GET_RR_REQ);
    seL4_SetMR(RSMSGREG_EXTRACT_RR_REQ_VADDR, (seL4_Word)remote_vaddr);
    seL4_SetMR(RSMSGREG_EXTRACT_RR_REQ_SIZE, size);
    seL4_SetMR(RSMSGREG_EXTRACT_RR_REQ_ID, res_id);
    seL4_SetMR(RSMSGREG_EXTRACT_RR_REQ_PD_ID, pd_id);
    seL4_SetMR(RSMSGREG_EXTRACT_RR_REQ_RS_PD_ID, server_pd_id);
    tag = seL4_Call(server_ep, tag);

    // Adjust result state's pointers if successful
    int result = seL4_MessageInfo_get_label(tag);
    if (result == seL4_NoError)
    {
        model_state_t *model_state = (model_state_t *)local_vaddr;

        gpi_model_state_component_t *old_mem_start = model_state->mem_start;
        model_state->mem_start = (gpi_model_state_component_t *)(local_vaddr + sizeof(model_state_t));
        model_state->mem_ptr = model_state->mem_ptr - old_mem_start + model_state->mem_start;

        *ret_state = model_state;
    }

    return result;
}

int resource_server_client_new_ns(seL4_CPtr server_ep,
                                  uint64_t *ns_id)
{
    RESOURCE_SERVER_PRINTF("Requesting new namespace from server ep (%d)\n", (int)server_ep);

    // Send IPC to resource server
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, RSMSGREG_NEW_NS_REQ_END);
    seL4_SetMR(RSMSGREG_FUNC, RS_FUNC_NEW_NS_REQ);
    tag = seL4_Call(server_ep, tag);

    int result = seL4_MessageInfo_get_label(tag);
    if (result == seL4_NoError)
    {
        *ns_id = seL4_GetMR(RSMSGREG_NEW_NS_ACK_ID);
    }

    return result;
}

int resource_server_create_resource(resource_server_context_t *context,
                                    uint64_t resource_id)
{
    int error;

    RESOURCE_SERVER_PRINTF("Creating resource with ID 0x%lx\n", resource_id);

    error = pd_client_create_resource(&context->pd_conn,
                                      context->server_id,
                                      resource_id);

    return error;
}

int resource_server_give_resource(resource_server_context_t *context,
                                  uint64_t ns_id,
                                  uint64_t resource_id,
                                  uint64_t client_id,
                                  seL4_CPtr *dest)
{
    int error;

    RESOURCE_SERVER_PRINTF("Giving resource to client, resource ID 0x%lx, client ID 0x%lx\n", resource_id, client_id);

    error = pd_client_give_resource(&context->pd_conn,
                                    context->server_id,
                                    ns_id,
                                    client_id,
                                    resource_id,
                                    dest);

    return error;
}

int resource_server_new_ns(resource_server_context_t *context,
                           uint64_t client_id,
                           uint64_t *ns_id)
{
    int error;

    RESOURCE_SERVER_PRINTF("Creating new NS\n");

    error = pd_client_register_namespace(&context->pd_conn, context->server_id, client_id, ns_id);

    return error;
}