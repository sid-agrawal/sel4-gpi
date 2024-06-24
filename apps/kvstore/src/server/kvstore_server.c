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

#define KVSTORE_DB_FILE_FORMAT "kvstore_%d.db"
#define MAX_KVSTORE_DBS_IN_FS 10 // Maximum number of kvstores in one file system

static const char *kvstore_db_file = "/kvstore.db";
static const char *create_table_cmd = "create table kvstore (key bigint unsigned not null primary key, val bigint unsigned);";
static const char *insert_format = "insert or replace into kvstore(key, val) values (%ld, %ld);";
static const char *select_format = "select val from kvstore where key == %ld;";

static sqlite3 *kvstore_db;
static int cmdlen = 128;
static char sql_cmd[128];
static char db_filename[128];
static char *errmsg = NULL;

static seL4_CPtr mcs_reply;

#define CHECK_ERROR(check, msg, err) \
    do                               \
    {                                \
        if ((check) != SQLITE_OK)    \
        {                            \
            ZF_LOGE("%s"             \
                    ", %d.",         \
                    msg,             \
                    error);          \
            return err;              \
        }                            \
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
        printf("SQL error in DB [%s]: %s\n", db_filename, errmsg);
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
        error = snprintf(db_filename, cmdlen, KVSTORE_DB_FILE_FORMAT, i);
        assert(error != -1);

        // Check if the file exists
        if (access(db_filename, F_OK))
        {
            KVSTORE_PRINTF("DB %s already exists, trying another name\n", db_filename);
        }
        else
        {
            break;
        }
    }

    KVSTORE_PRINTF("Creating DB %s\n", db_filename);
    error = sqlite3_open(db_filename, &kvstore_db);
    CHECK_ERROR(error, "failed to open kvstore db", KVSTORE_ERROR_UNKNOWN);
    KVSTORE_PRINTF("Created DB %s\n", db_filename);

    SQL_EXEC(kvstore_db, create_table_cmd, NULL);
    CHECK_ERROR(error, "failed to create kvstore table", KVSTORE_ERROR_UNKNOWN);
    KVSTORE_PRINTF("Created table\n");

    return error;
}

int kvstore_server_main(seL4_CPtr parent_ep)
{
    int error;
    seL4_MessageInfo_t tag;
    seL4_CPtr badge;

    seL4_CPtr fs_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME));
    ep_client_context_t parent_ep_conn = {.badged_server_ep_cspath.capPtr = parent_ep};
    error = ep_client_get_raw_endpoint(&parent_ep_conn);
    CHECK_ERROR(error, "Failed to retrieve parent EP\n", KVSTORE_ERROR_UNKNOWN);

    printf("kvstore-server main: parent ep (%d), fs ep(%d) \n", (int)parent_ep_conn.raw_endpoint, (int)fs_ep);

    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();

    /* initialize server */
    error = kvstore_server_init();
    CHECK_ERROR(error, "failed to initialize kvstore server\n", KVSTORE_ERROR_UNKNOWN);

    /* allocate our own endpoint */
    ep_client_context_t ep_conn;
    error = sel4gpi_alloc_endpoint(&ep_conn);
    CHECK_ERROR(error, "failed to allocate endpoint\n", KVSTORE_ERROR_UNKNOWN);

    /* notify parent that we have started */
    KVSTORE_PRINTF("Messaging parent process at slot %d, sending ep (%d)\n", (int)parent_ep_conn.raw_endpoint, (int)ep_conn.raw_endpoint);
    tag = seL4_MessageInfo_new(0, 0, 1, 0);
    seL4_SetCap(0, ep_conn.raw_endpoint);
    seL4_Send(parent_ep_conn.raw_endpoint, tag);

    /* start serving requests */
    while (1)
    {
        /* Receive a message */
        KVSTORE_PRINTF("Ready to receive a message\n");
        tag = recv(ep_conn.raw_endpoint, &badge);
        int op = seL4_GetMR(KVMSGREG_FUNC);
        KVSTORE_PRINTF("Received message\n");

        if (op == KV_FUNC_SET_REQ)
        {
            seL4_Word key = seL4_GetMR(KVMSGREG_SET_REQ_KEY);
            seL4_Word val = seL4_GetMR(KVMSGREG_SET_REQ_VAL);

            error = kvstore_server_set(key, val);

            // Restore state of message registers for reply
            seL4_MessageInfo_ptr_set_length(&tag, KVMSGREG_SET_ACK_END);
            seL4_MessageInfo_ptr_set_label(&tag, error);
            seL4_SetMR(KVMSGREG_FUNC, KV_FUNC_SET_ACK);
        }
        else if (op == KV_FUNC_GET_REQ)
        {
            seL4_Word key = seL4_GetMR(KVMSGREG_GET_REQ_KEY);
            seL4_Word val;

            error = kvstore_server_get(key, &val);

            // Restore state of message registers for reply
            seL4_MessageInfo_ptr_set_length(&tag, KVMSGREG_GET_ACK_END);
            seL4_MessageInfo_ptr_set_label(&tag, error);
            seL4_SetMR(KVMSGREG_GET_ACK_VAL, val);
            seL4_SetMR(KVMSGREG_FUNC, KV_FUNC_GET_ACK);
        }
        else
        {
            KVSTORE_PRINTF("Got invalid opcode (%d)\n", op);
        }

        /* Reply to message */
        reply(tag);
    }

main_exit:
    /* notify parent that we have failed */
    KVSTORE_PRINTF("Messaging parent process at slot %d, notifying of failure\n", (int)parent_ep_conn.raw_endpoint);
    tag = seL4_MessageInfo_new(error, 0, 0, 0);
    seL4_Send(parent_ep_conn.raw_endpoint, tag);
}

static void kvstore_server_main_thread(void *arg0, void *arg1, void *arg2)
{
    seL4_CPtr parent_ep = (seL4_CPtr)arg0;
    printf("kvstore-server: in thread, parent ep (%d) \n", (int)parent_ep);

    printf("arg0 %p, arg1 %p arg2 %p\n", arg0, arg1, arg2);

    kvstore_server_main(parent_ep);
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
    sel4gpi_add_rde_config(cfg, GPICAP_TYPE_EP, RESSPC_ID_NULL);

    /* temp EP */
    ep_client_context_t ep_conn;
    error = sel4gpi_alloc_endpoint(&ep_conn);
    GOTO_IF_ERR(error, "failed to allocate ep\n");

    seL4_CPtr temp_ep_in_PD;
    pd_client_send_cap(&runnable.pd, ep_conn.badged_server_ep_cspath.capPtr, &temp_ep_in_PD);

    error = sel4gpi_prepare_pd(cfg, &runnable, 1, (seL4_Word *)&temp_ep_in_PD);
    GOTO_IF_ERR(error, "Failed to prepare PD\n");

    error = sel4gpi_start_pd(&runnable);
    GOTO_IF_ERR(error, "Failed to start PD\n");

    seL4_CPtr receive_slot;
    error = pd_client_next_slot(&self_pd_conn, &receive_slot);
    seL4_SetCapReceivePath(PD_CAP_ROOT, receive_slot, PD_CAP_DEPTH);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(ep_conn.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    CHECK_ERROR(error, "kvstore thread setup failed", KVSTORE_ERROR_UNKNOWN);

    *kvstore_ep = receive_slot;
    KVSTORE_PRINTF("Started thread, ep (%d)\n", (int)receive_slot);

    sel4gpi_config_destroy(cfg);

err_goto:
    return error;
}

int kvstore_server_set(seL4_Word key, seL4_Word value)
{
    KVSTORE_PRINTF("kvstore_server_set: key (%ld), value (%ld), %s\n", key, value, db_filename);

    int error = seL4_NoError;
    SQL_EXEC(kvstore_db, insert_format, key, value);
    CHECK_ERROR(error, "failed to insert pair to kvstore table", KVSTORE_ERROR_UNKNOWN);
    return error;
}

int kvstore_server_get(seL4_Word key, seL4_Word *value)
{
    KVSTORE_PRINTF("kvstore_server_get: key (%ld)\n", key);

    int error = seL4_NoError;
    sqlite3_stmt *stmt;

    SQL_MAKE_CMD(select_format, key);
    error = sqlite3_prepare_v2(kvstore_db, sql_cmd, -1, &stmt, 0);
    CHECK_ERROR(error, "failed to prepare sql cmd", KVSTORE_ERROR_UNKNOWN);

    // Execute the statement (gets one row if it exists)
    int res = sqlite3_step(stmt);
    if (res != SQLITE_ROW)
    {
        // This means there was no data found for the key
        error = sqlite3_finalize(stmt);
        CHECK_ERROR(error, "failed to finalize sql cmd", KVSTORE_ERROR_UNKNOWN);
        return KVSTORE_ERROR_KEY;
    }

    // Retrieve value
    const char *val_s = (const char *)sqlite3_column_text(stmt, 0);
    CHECK_ERROR(val_s == NULL, "failed to get value from row", KVSTORE_ERROR_KEY);

    *value = atoi(val_s);
    error = sqlite3_finalize(stmt);
    CHECK_ERROR(error, "failed to finalize sql cmd", KVSTORE_ERROR_UNKNOWN);

    return error;
}