/**
 * @file The kvstore server functionality
 */

#include <stdlib.h>
#include <utils/uthash.h>

#include <sqlite3/sqlite3.h>
#include <fs_client.h>
#include <kvstore_server.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/pd_creation.h>
#include <sel4gpi/error_handle.h>
#include <sel4runtime.h>
#include <kvstore_server_rpc.pb.h>

#define KVSTORE_DB_FILE_FORMAT "kvstore_%d.db"
#define MAX_KVSTORE_DBS_IN_FS 10 // Maximum number of kvstores in one file system
#define DEFAULT_KVSTORE_OBJ_ID 1

static const char *kvstore_db_file = "/kvstore.db";
static const char *create_table_cmd = "create table kvstore (key bigint unsigned not null primary key, val bigint unsigned);";
static const char *insert_format = "insert or replace into kvstore(key, val) values (%ld, %ld);";
static const char *select_format = "select val from kvstore where key == %ld;";

static sqlite3 *kvstore_db;
static int cmdlen = 128;
static char sql_cmd[128];
static char *errmsg = NULL;
static kvstore_server_context_t kvstore_server = {0};

static seL4_CPtr mcs_reply;

#define CHECK_ERR_GOTO(check, msg, err) \
    do                                  \
    {                                   \
        if ((check) != seL4_NoError)    \
        {                               \
            ZF_LOGE("%s",               \
                    msg);               \
            error = err;                \
            goto err_goto;              \
        }                               \
    } while (0);

#define SQL_MAKE_CMD(format, ...)                               \
    do                                                          \
    {                                                           \
        error = snprintf(sql_cmd, cmdlen, format, __VA_ARGS__); \
        assert(error != -1);                                    \
    } while (0);

#define SQL_EXEC(sql_db, format, ...)                                          \
    do                                                                         \
    {                                                                          \
        error = snprintf(sql_cmd, cmdlen, format, __VA_ARGS__);                \
        assert(error != -1);                                                   \
        error = sqlite3_exec(sql_db, sql_cmd, sqlite_callback, NULL, &errmsg); \
        print_error(error, errmsg, sql_db);                                    \
    } while (0);

kvstore_server_context_t *get_kvstore_server(void)
{
    return &kvstore_server;
}

static seL4_MessageInfo_t recv(seL4_CPtr ep, seL4_Word *sender_badge_ptr)
{
    /** NOTE:

     * the reply param of api_recv(third param) is only used in the MCS kernel.
     **/

    return api_recv(ep,
                    sender_badge_ptr,
                    mcs_reply);
}

static void reply(seL4_MessageInfo_t tag)
{
    api_reply(mcs_reply, tag);
}

/**
 * If there is an error, display details
 */
static void print_error(int error, char *errmsg, sqlite3 *db)
{
    if (error != SQLITE_OK)
    {
        printf("SQL error in DB [%s]: %s\n", get_kvstore_server()->db_filename, errmsg);
        sqlite3_free(errmsg);
        printf("sqlite3_exec error, extended errcode: %d\n", sqlite3_extended_errcode(db));
    }
}

/**
 * Callback for executed SQL statements
 * No-op
 */
static int sqlite_callback(void *listp, int argc, char **argv, char **azColName)
{
    return 0;
}

int kvstore_server_init()
{
    KVSTORE_PRINTF("kvstore_server_init\n");

    int error = 0;

    /* initialize as a file system client */
    error = xv6fs_client_init();

    /* setup the sqlite db/table */

    /* Try a few filenames in case there is already a kvstore db */
    for (int i = 0; i < MAX_KVSTORE_DBS_IN_FS; i++)
    {
        error = snprintf(get_kvstore_server()->db_filename, cmdlen, KVSTORE_DB_FILE_FORMAT, i);
        assert(error != -1);

        // Check if the file exists
        if (access(get_kvstore_server()->db_filename, F_OK))
        {
            KVSTORE_PRINTF("DB %s already exists, trying another name\n", get_kvstore_server()->db_filename);
        }
        else
        {
            break;
        }
    }

    KVSTORE_PRINTF("Creating DB %s\n", get_kvstore_server()->db_filename);
    error = sqlite3_open(get_kvstore_server()->db_filename, &kvstore_db);
    CHECK_ERR_GOTO(error, "failed to open kvstore db", KvstoreError_UNKNOWN);
    KVSTORE_PRINTF("Created DB %s\n", get_kvstore_server()->db_filename);

    SQL_EXEC(kvstore_db, create_table_cmd, NULL);
    CHECK_ERR_GOTO(error, "failed to create kvstore table", KvstoreError_UNKNOWN);
    KVSTORE_PRINTF("Created table\n");

err_goto:
    return error;
}

// Internal init function for resource server utils
static int kvstore_init()
{
    int error = 0;

    error = kvstore_server_init();
    CHECK_ERR_GOTO(error, "failed to initialize kvstore files", KvstoreError_UNKNOWN);

    get_kvstore_server()->kvstore_obj_id = BADGE_OBJ_ID_NULL;

    error = resspc_client_map_space(
        &get_kvstore_server()->gen.default_space,
        sel4gpi_get_default_space_id(sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME)));
    CHECK_ERR_GOTO(error, "failed to map kvstore space to file space", KvstoreError_UNKNOWN);

err_goto:
}

static void kvstore_request_handler(void *msg_p,
                                    void *msg_reply_p,
                                    seL4_Word sender_badge,
                                    seL4_CPtr cap, bool *need_new_recv_cap)
{
    int error = 0;
    void *mo_vaddr;
    *need_new_recv_cap = false;
    KvstoreMessage *msg = (KvstoreMessage *)msg_p;
    KvstoreReturnMessage *reply_msg = (KvstoreReturnMessage *)msg_reply_p;
    reply_msg->which_msg = KvstoreReturnMessage_basic_tag;

    CHECK_ERR_GOTO(msg->magic != KVSTORE_RPC_MAGIC,
                   "KVstore server received message with incorrect magic number\n",
                   KvstoreError_UNKNOWN);

    // Get info from badge
    gpi_obj_id_t client_id = get_client_id_from_badge(sender_badge);
    gpi_obj_id_t obj_id = get_object_id_from_badge(sender_badge);
    gpi_space_id_t ns_id = get_space_id_from_badge(sender_badge);
    gpi_cap_t cap_type = get_cap_type_from_badge(sender_badge);

    CHECK_ERR_GOTO(sender_badge == 0, "Got message on unbadged ep", KvstoreError_UNKNOWN);
    CHECK_ERR_GOTO(cap_type != get_kvstore_server()->gen.resource_type, "Got invalid captype",
                   KvstoreError_UNKNOWN);

    // Handle the message
    if (obj_id == BADGE_OBJ_ID_NULL)
    { /* Handle Request Not Associated to Object */
        KVSTORE_PRINTF("Received badged request with no object id\n");

        char *pathname;

        switch (msg->which_msg)
        {
        case KvstoreMessage_create_tag:
            KVSTORE_PRINTF("Got create message from client %u\n", client_id);
            // Give the client a resource to access the kvstore
            // (XXX) Arya: This doesn't actually create a new kvstore because the server currently
            // only supports a single kvstore

            if (get_kvstore_server()->kvstore_obj_id == BADGE_OBJ_ID_NULL)
            {
                error = resource_server_create_resource(&get_kvstore_server()->gen,
                                                        &get_kvstore_server()->gen.default_space,
                                                        DEFAULT_KVSTORE_OBJ_ID);
                CHECK_ERR_GOTO(error, "Failed to create the kvstore resource", KvstoreError_UNKNOWN);

                get_kvstore_server()->kvstore_obj_id = DEFAULT_KVSTORE_OBJ_ID;
            }

            seL4_CPtr dest;
            error = resource_server_give_resource(&get_kvstore_server()->gen,
                                                  get_kvstore_server()->gen.default_space.id,
                                                  get_kvstore_server()->kvstore_obj_id,
                                                  client_id,
                                                  &dest);
            CHECK_ERR_GOTO(error, "Failed to give the kvstore resource", KvstoreError_UNKNOWN);

            reply_msg->which_msg = KvstoreReturnMessage_alloc_tag;
            reply_msg->msg.alloc.dest = dest;
            break;
        default:
            CHECK_ERR_GOTO(1, "got invalid op on badged ep without obj id", KvstoreError_UNKNOWN);
        }
    }
    else
    {
        /* Handle Request On Specific Resource */
        KVSTORE_PRINTF("Received badged request with object id 0x%u\n", get_object_id_from_badge(sender_badge));

        switch (msg->which_msg)
        {
        case KvstoreMessage_set_tag:
            error = kvstore_server_set(msg->msg.set.key, msg->msg.set.val);
            break;
        case KvstoreMessage_get_tag:
            uint64_t val;
            error = kvstore_server_get(msg->msg.get.key, &val);

            reply_msg->which_msg = KvstoreReturnMessage_get_tag;
            reply_msg->msg.get.val = val;
            break;
        default:
            CHECK_ERR_GOTO(1, "got invalid op on badged ep with obj id", KvstoreError_UNKNOWN);
        }
    }

err_goto:
    reply_msg->errorCode = error;
}

static int kvstore_work_handler(PdWorkReturnMessage *work)
{
    int error = 0;
    seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(0, 0, 0, 0);

    int op = work->action;
    if (op == PdWorkAction_EXTRACT)
    {
        KVSTORE_PRINTF("Got EXTRACT work from RT\n");

        /* Initialize the model state */
        mo_client_context_t mo;
        model_state_t *model_state;
        error = resource_server_extraction_setup(&get_kvstore_server()->gen, 4, &mo, &model_state);
        CHECK_ERR_GOTO(error, "Failed to setup model extraction\n", KvstoreError_UNKNOWN);

        for (int i = 0; i < work->object_ids_count; i++)
        {
            gpi_space_id_t space_id = work->space_ids[i];
            gpi_obj_id_t fileno = work->object_ids[i];
            gpi_obj_id_t client_pd_id = work->pd_ids[i];

            if (fileno != BADGE_OBJ_ID_NULL)
            {
                /* File system only does extraction at a space-level, not at a file-level */
                continue;
            }
        }

        /* Send the result */
        error = resource_server_extraction_finish(&get_kvstore_server()->gen, &mo, model_state, work->object_ids_count);
        CHECK_ERR_GOTO(error, "Failed to finish model extraction\n", KvstoreError_UNKNOWN);
    }
    else if (op == PdWorkAction_FREE)
    {
        KVSTORE_PRINTF("Got FREE work from RT\n");
        for (int i = 0; i < work->object_ids_count; i++)
        {
            // Actually don't do much when a file is freed, it's the same as file close
            // We still want to keep it in the file system, but we can reduce the refcount
            gpi_obj_id_t file_id = work->object_ids[i];
        }

        error = pd_client_finish_work(&get_kvstore_server()->gen.pd_conn, work->object_ids_count, work->n_critical);
    }
    else if (op == PdWorkAction_DESTROY)
    {
        KVSTORE_PRINTF("Got DESTROY work from RT\n");
        for (int i = 0; i < work->object_ids_count; i++)
        {
            gpi_obj_id_t file_id = work->object_ids[i];
            gpi_space_id_t space_id = work->space_ids[i];

            if (file_id != BADGE_OBJ_ID_NULL)
            {
                // Destroy a particular file
            }
            else
            {
                // Destroy a whole space
            }
        }

        error = pd_client_finish_work(&get_kvstore_server()->gen.pd_conn, work->object_ids_count, work->n_critical);
    }
    else
    {
        KVSTORE_PRINTF("Unknown work action\n");
        error = 1;
    }

err_goto:
    return error;
}

int kvstore_server_main(seL4_CPtr parent_ep, gpi_obj_id_t parent_pd_id)
{
    return resource_server_start(
        &get_kvstore_server()->gen,
        KVSTORE_RESOURCE_NAME,
        kvstore_request_handler,
        kvstore_work_handler,
        parent_ep,
        parent_pd_id,
        kvstore_init,
        FS_DEBUG_ENABLED,
        &KvstoreMessage_msg,
        &KvstoreReturnMessage_msg);
}

static void kvstore_server_main_thread(int argc, char **argv)
{
    if (argc == 0)
    {
        printf("kvstore-server, no arguments given\n");
    }
    else
    {
        seL4_CPtr parent_ep = (seL4_CPtr)atol(argv[0]);
        gpi_obj_id_t parent_pd_id = (seL4_CPtr)atol(argv[1]);
        printf("kvstore-server: in thread, parent ep (%lu), parent ID (%u) \n", parent_ep, parent_pd_id);

        kvstore_server_main(parent_ep, parent_pd_id);
    }
}

int kvstore_server_start_thread(seL4_CPtr *kvstore_ep)
{
    int error;
    pd_client_context_t self_pd_conn = sel4gpi_get_pd_conn();
    seL4_CPtr pd_rde = sel4gpi_get_rde(GPICAP_TYPE_PD);
    GOTO_IF_COND(pd_rde == seL4_CapNull, "Can't start thread, no PD RDE\n");

    /* new PD as the thread */
    sel4gpi_runnable_t runnable = {0};
    pd_config_t *cfg = sel4gpi_configure_thread(kvstore_server_main_thread, seL4_CapNull, &runnable);
    GOTO_IF_COND(cfg == NULL, "Failed to generate a thread config\n");

    /* allow KVstore to allocate new EPs */
    sel4gpi_add_rde_config(cfg, GPICAP_TYPE_EP, BADGE_SPACE_ID_NULL);

    /* temp EP */
    ep_client_context_t ep_conn;
    error = sel4gpi_alloc_endpoint(&ep_conn);
    CHECK_ERR_GOTO(error, "failed to allocate ep\n", KvstoreError_UNKNOWN);

    seL4_CPtr temp_ep_in_PD;
    pd_client_send_cap(&runnable.pd, ep_conn.ep, &temp_ep_in_PD);

    /* prepare args */
    int argc = 2;
    gpi_obj_id_t self_pd_id = sel4gpi_get_pd_conn().id;
    seL4_Word args[2] = {temp_ep_in_PD, self_pd_id};

    /* start the PD */
    error = sel4gpi_prepare_pd(cfg, &runnable, argc, args);
    CHECK_ERR_GOTO(error, "Failed to prepare PD\n", KvstoreError_UNKNOWN);

    error = sel4gpi_start_pd(&runnable);
    CHECK_ERR_GOTO(error, "Failed to start PD\n", KvstoreError_UNKNOWN);

    /* wait for te thread to start */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(ep_conn.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    CHECK_ERR_GOTO(error, "kvstore thread setup failed", KvstoreError_UNKNOWN);

    /* get the kvstore EP from RDE */
    gpi_cap_t kvstore_type_code = sel4gpi_get_resource_type_code(KVSTORE_RESOURCE_NAME);
    CHECK_ERR_GOTO(kvstore_type_code == GPICAP_TYPE_NONE, "failed to get type code for kvstore", KvstoreError_UNKNOWN);
    *kvstore_ep = sel4gpi_get_rde(kvstore_type_code);
    CHECK_ERR_GOTO(*kvstore_ep == seL4_CapNull, "failed to get RDE for kvstore", KvstoreError_UNKNOWN);

    KVSTORE_PRINTF("Started thread, ep (%lu)\n", *kvstore_ep);

    sel4gpi_config_destroy(cfg);

    // (XXX) Arya: should clean up the temporary EP here
    // At least, it will be freed when the PD terminates

err_goto:
    return error;
}

int kvstore_server_set(seL4_Word key, seL4_Word value)
{
    KVSTORE_PRINTF("kvstore_server_set: key (%ld), value (%ld), %s\n", key, value, get_kvstore_server()->db_filename);

    int error = seL4_NoError;
    SQL_EXEC(kvstore_db, insert_format, key, value);
    CHECK_ERR_GOTO(error, "failed to insert pair to kvstore table", KvstoreError_UNKNOWN);

err_goto:
    return error;
}

int kvstore_server_get(seL4_Word key, seL4_Word *value)
{
    KVSTORE_PRINTF("kvstore_server_get: key (%ld)\n", key);

    int error = seL4_NoError;
    sqlite3_stmt *stmt;

    SQL_MAKE_CMD(select_format, key);
    error = sqlite3_prepare_v2(kvstore_db, sql_cmd, -1, &stmt, 0);
    CHECK_ERR_GOTO(error, "failed to prepare sql cmd", KvstoreError_UNKNOWN);

    // Execute the statement (gets one row if it exists)
    int res = sqlite3_step(stmt);
    if (res != SQLITE_ROW)
    {
        // This means there was no data found for the key
        error = sqlite3_finalize(stmt);
        CHECK_ERR_GOTO(error, "failed to finalize sql cmd", KvstoreError_UNKNOWN);
        return KvstoreError_KEY;
    }

    // Retrieve value
    const char *val_s = (const char *)sqlite3_column_text(stmt, 0);
    CHECK_ERR_GOTO(val_s == NULL, "failed to get value from row", KvstoreError_KEY);

    *value = atoi(val_s);
    error = sqlite3_finalize(stmt);
    CHECK_ERR_GOTO(error, "failed to finalize sql cmd", KvstoreError_UNKNOWN);

err_goto:
    return error;
}