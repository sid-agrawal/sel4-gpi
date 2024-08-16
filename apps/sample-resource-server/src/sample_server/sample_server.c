#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vka/vka.h>
#include <vspace/vspace.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/vmr_clientapi.h>

#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/resource_server_utils.h>
#include <sel4gpi/model_exporting.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/gpi_rpc.h>
#include <sample_rpc.pb.h>

#include <sample_server.h>

#define SAMPLE_S "Sample Server: "

#if SAMPLE_DEBUG
#define SAMPLE_PRINTF(...)       \
    do                           \
    {                            \
        printf("%s ", SAMPLE_S); \
        printf(__VA_ARGS__);     \
    } while (0);
#else
#define SAMPLE_PRINTF(...)
#endif

#define CHECK_ERROR(error, msg)       \
    do                                \
    {                                 \
        if (error != seL4_NoError)    \
        {                             \
            ZF_LOGE(SAMPLE_S "%s: %s" \
                             ", %d.", \
                    __func__,         \
                    msg,              \
                    error);           \
            return error;             \
        }                             \
    } while (0);

#define CHECK_ERROR_GOTO(check, msg, err, loc) \
    do                                         \
    {                                          \
        if ((check) != seL4_NoError)           \
        {                                      \
            ZF_LOGE(SAMPLE_S "%s: %s"          \
                             ", %d.\n",        \
                    __func__,                  \
                    msg,                       \
                    check);                    \
            error = err;                       \
            goto loc;                          \
        }                                      \
    } while (0);

static sample_server_context_t sample_server;

sample_server_context_t *get_sample_server(void)
{
    return &sample_server;
}

/**
 * This is called as a callback by the resource registry when an entry is about to be deleted
 * from the resource registry. Perform any necessary cleanup here, before the node structure is freed.
 */
static void on_resource_registry_entry_delete(resource_registry_node_t *node_gen, void *arg0)
{
    int error = 0;
    sample_resource_registry_entry_t *node = (sample_resource_registry_entry_t *)node_gen;

    SAMPLE_PRINTF("Deleting resource: id = %lu, x = %lu\n", node_gen->object_id, node->x);

    /* INSERT HERE on-deletion behaviour */

    return;

err_goto:
    ZF_LOGE("Error while deleting a sample resource registry entry\n");
}

int sample_init()
{
    sample_server_context_t *server = get_sample_server();
    int error = 0;

    // Initialize the resource registry
    resource_registry_initialize(&get_sample_server()->registry,
                                 on_resource_registry_entry_delete, NULL,
                                 BADGE_MAX_OBJ_ID - 1);

    /**
     * INSERT HERE initialization code
     * For example, if the resource space maps to another space:
     * error = resspc_client_map_space(&get_sample_server()->gen.default_space,
     *                                 sel4gpi_get_default_space_id(...));
     **/

    return error;
}

void sample_request_handler(
    void *msg_p,
    void *msg_reply_p,
    seL4_Word sender_badge,
    seL4_CPtr cap,
    bool *need_new_recv_cap)
{
    int error = 0;
    void *mo_vaddr;
    *need_new_recv_cap = false;
    SampleMessage *msg = (SampleMessage *)msg_p;
    SampleReturnMessage *reply_msg = (SampleReturnMessage *)msg_reply_p;
    reply_msg->which_msg = SampleReturnMessage_basic_tag;

    // Check the RPC magic is correct
    CHECK_ERROR_GOTO(msg->magic != SAMPLE_RPC_MAGIC,
                     "Sample server received message with incorrect magic number\n",
                     SampleError_BAD_ARG,
                     done);

    // Get info from badge
    gpi_obj_id_t client_id = get_client_id_from_badge(sender_badge);
    gpi_obj_id_t obj_id = get_object_id_from_badge(sender_badge);
    gpi_cap_t cap_type = get_cap_type_from_badge(sender_badge);

    // Check the cap type is correct
    CHECK_ERROR_GOTO(sender_badge == 0, "Got message on unbadged ep", SampleError_UNKNOWN, done);
    CHECK_ERROR_GOTO(cap_type != get_sample_server()->gen.resource_type, "Got invalid captype",
                     SampleError_UNKNOWN, done);

    if (obj_id == BADGE_OBJ_ID_NULL)
    {
        SAMPLE_PRINTF("Got message on EP with no object id: %lx\n", sender_badge);

        switch (msg->which_msg)
        {
        case SampleMessage_alloc_tag:
            SAMPLE_PRINTF("Got request to allocate a new resource\n");

            // Add a new resource in the registry
            sample_resource_registry_entry_t *node = calloc(1, sizeof(sample_resource_registry_entry_t));
            CHECK_ERROR_GOTO(node == NULL, "couldn't allocate resource node, out of memory",
                             SampleError_SERVER_ERR, done);

            /* INSERT HERE resource initialization */

            gpi_obj_id_t id = resource_registry_insert_new_id(
                &get_sample_server()->registry, (resource_registry_node_t *)node);

            // Create the resource
            error = resource_server_create_resource(&get_sample_server()->gen,
                                                    &get_sample_server()->gen.default_space,
                                                    id);
            CHECK_ERROR_GOTO(error, "Failed to create the resource", SampleError_UNKNOWN, done);

            // Give the resource endpoint
            seL4_CPtr dest;
            error = resource_server_give_resource(&get_sample_server()->gen,
                                                  get_space_id_from_badge(sender_badge),
                                                  id,
                                                  get_client_id_from_badge(sender_badge),
                                                  &dest);
            CHECK_ERROR_GOTO(error, "Failed to give the resource", SampleError_UNKNOWN, done);

            // Send the reply
            reply_msg->which_msg = SampleReturnMessage_alloc_tag;
            reply_msg->msg.alloc.dest = dest;
            break;
        default:
            CHECK_ERROR_GOTO(1, "got invalid op on EP without obj id", SampleError_BAD_ARG, done);
        }
    }
    else
    {
        SAMPLE_PRINTF("Got message on EP with badge: %lx\n", sender_badge);

        // Find the resource from the registry
        sample_resource_registry_entry_t *node = (sample_resource_registry_entry_t *)
            resource_registry_get_by_id(&get_sample_server()->registry, obj_id);
        CHECK_ERROR_GOTO(node == NULL, "couldn't find resource in registry", SampleError_SERVER_ERR, done);

        switch (msg->which_msg)
        {
        case SampleMessage_invoke_tag:
            SAMPLE_PRINTF("Got request to invoke a resource\n");

            // Update the resource
            node->x = msg->msg.invoke.x;

            // Do something
            uint64_t result = msg->msg.invoke.x + msg->msg.invoke.y;

            /* INSERT HERE invocation functionality */

            // Send the reply
            reply_msg->which_msg = SampleReturnMessage_invoke_tag;
            snprintf(reply_msg->msg.invoke.z, sizeof(reply_msg->msg.invoke.z), "0x%lx", result);
            break;
        case SampleMessage_free_tag:
            SAMPLE_PRINTF("Got request to free a resource\n");

            if (node->gen.count == 1)
            {
                // Notify the root task that the resource is deleted
                error = resspc_client_delete_resource(&get_sample_server()->gen.default_space, obj_id);
                CHECK_ERROR_GOTO(error, "Failed to delete resource", SampleError_UNKNOWN, done);
            }
            else
            {
                // The resource won't be deleted yet, just revoke the resource from the client
                error = resspc_client_revoke_resource(&get_sample_server()->gen.default_space, obj_id, client_id);
                CHECK_ERROR_GOTO(error, "Failed to revoke resource from client", SampleError_UNKNOWN, done);
            }

            // Decrement the resource refcount in the registry
            // If the refcount reaches zero, then on_resource_registry_entry_delete will be called
            resource_registry_dec(&get_sample_server()->registry, (resource_registry_node_t *)node);

            break;

            /* INSERT HERE more cases */

        default:
            CHECK_ERROR_GOTO(1, "got invalid op on EP with obj id", SampleError_BAD_ARG, done);
        }
    }

done:
    reply_msg->errorCode = error;
}

int sample_work_handler(PdWorkReturnMessage *work)
{
    int error = 0;
    seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);

    int op = work->action;
    if (op == PdWorkAction_EXTRACT)
    {
        gpi_obj_id_t sample_pd_id = sel4gpi_get_pd_conn().id;

        // If there is no data to send, replace the model extraction with the block below
#if 0
        error = resource_server_extraction_no_data(&get_sample_server()->gen, work->object_ids_count);
        CHECK_ERROR_GOTO(error, "Failed to finish model extraction\n", SampleError_UNKNOWN, err_goto);
#endif

        /* Initialize the model state */
        mo_client_context_t mo;
        model_state_t *model_state;
        error = resource_server_extraction_setup(&get_sample_server()->gen, 4, &mo, &model_state);
        CHECK_ERROR_GOTO(error, "Failed to setup model extraction\n", SampleError_UNKNOWN, err_goto);

        for (int i = 0; i < work->object_ids_count; i++)
        {
            gpi_space_id_t space_id = work->space_ids[i];
            gpi_obj_id_t object_id = work->object_ids[i];
            gpi_obj_id_t client_pd_id = work->pd_ids[i];

            if (object_id == BADGE_OBJ_ID_NULL)
            {
                /* INSERT HERE update model state at space-level, if applicable */

                // For example:
#if 0
                // Get the sample server PD ID string
                char sample_pd_id_str[CSV_MAX_STRING_SIZE];
                get_pd_id(sel4gpi_get_pd_conn().id, sample_pd_id_str);

                // Get the client PD ID string
                char client_pd_id_str[CSV_MAX_STRING_SIZE];
                get_pd_id(client_pd_id, client_pd_id_str);

                // Get the sample resource space ID string
                char sample_space_id[CSV_MAX_STRING_SIZE];
                get_resource_space_id(get_sample_server()->gen.resource_type,
                                      get_sample_server()->gen.default_space.id,
                                      sample_space_id);

                // Find the implicit resource(s)
                gpi_obj_id_t id = ...;

                // Add the sample resource node(s)
                // These would be implicit resources, which the root task does not add to the model state
                gpi_model_node_t *sample_node = add_resource_node(
                    model_state,
                    make_res_id(get_sample_server()->gen.resource_type, get_sample_server()->gen.default_space.id, id),
                    true);

                // Add the subset edge
                add_edge_by_id(model_state, GPI_EDGE_TYPE_SUBSET, sample_node->id, sample_space_id);

                // Add the hold edges
                add_edge_by_id(model_state, GPI_EDGE_TYPE_HOLD, sample_pd_id_str, sample_node->id);
                add_edge_by_id(model_state, GPI_EDGE_TYPE_HOLD, client_pd_id_str, sample_node->id);

                // Add the map edges
                add_edge(model_state, GPI_EDGE_TYPE_MAP, sample_node, ...);
#endif
            }
            else
            {
                /* INSERT HERE update model state at resource-level, if applicable */

                // For example:
#if 0
                // Get the sample resource node id
                // The node is added to the model state by the root task
                char sample_node_id[CSV_MAX_STRING_SIZE];
                get_resource_id(
                    make_res_id(get_sample_server()->gen.resource_type,
                                get_sample_server()->gen.default_space.id,
                                object_id),
                    sample_node_id);

                // Add the map edge(s)
                add_edge_by_id(model_state, GPI_EDGE_TYPE_MAP, sample_node_id, ...);
#endif
            }
        }

        /* Send the result */
        error = resource_server_extraction_finish(&get_sample_server()->gen, &mo, model_state, work->object_ids_count);
    }
    else if (op == PdWorkAction_FREE)
    {
        for (int i = 0; i < work->object_ids_count; i++)
        {
            gpi_space_id_t space_id = work->space_ids[i];
            gpi_obj_id_t object_id = work->object_ids[i];

            assert(space_id == get_sample_server()->gen.default_space.id);

            // Find the resource from the registry
            sample_resource_registry_entry_t *node = (sample_resource_registry_entry_t *)
                resource_registry_get_by_id(&get_sample_server()->registry, object_id);
            CHECK_ERROR_GOTO(node == NULL, "couldn't find resource in registry", SampleError_SERVER_ERR, err_goto);

            // Reduce refcount of the resource
            resource_registry_dec(&get_sample_server()->registry, (resource_registry_node_t *)node);
        }

        // Notify the resource server that the work is done
        error = pd_client_finish_work(&get_sample_server()->gen.pd_conn, work->object_ids_count, work->n_critical);
    }
    else if (op == PdWorkAction_DESTROY)
    {
        for (int i = 0; i < work->object_ids_count; i++)
        {
            gpi_space_id_t space_id = work->space_ids[i];
            gpi_obj_id_t object_id = work->object_ids[i];

            assert(space_id == get_sample_server()->gen.default_space.id);
            assert(object_id == BADGE_OBJ_ID_NULL); // Only resource spaces should be destroyed

            /* INSERT HERE space cleanup */
        }

        // Notify the resource server that the work is done
        error = pd_client_finish_work(&get_sample_server()->gen.pd_conn, work->object_ids_count, work->n_critical);
    }
    else
    {
        SAMPLE_PRINTF("Unknown work action\n");
        error = 1;
    }

err_goto:
    return error;
}
