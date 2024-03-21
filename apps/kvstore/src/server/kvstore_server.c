/**
 * @file The kvstore server functionality
 */

#include <stdlib.h>
#include <utils/uthash.h>

#include <sqlite3/sqlite3.h>
#include <fs_client.h>
#include <kvstore_server.h>

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

#define CHECK_ERROR(check, msg)        \
    do                                 \
    {                                  \
        if ((check) != SQLITE_OK)      \
        {                              \
            ZF_LOGE("%s"               \
                    ", %d.",           \
                    msg,               \
                    error);            \
            error = KVSTORE_ERROR_KEY; \
            return error;              \
        }                              \
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

    error = sqlite3_open(db_filename, &kvstore_db);

    KVSTORE_PRINTF("Created DB %s\n", db_filename);

    CHECK_ERROR(error, "failed to open kvstore db");
    assert(kvstore_db != NULL);

    SQL_EXEC(kvstore_db, create_table_cmd, NULL);
    CHECK_ERROR(error, "failed to create kvstore table");

    return error;
}

int kvstore_server_set(seL4_Word key, seL4_Word value)
{
    KVSTORE_PRINTF("kvstore_server_set: key (%ld), value (%ld), %s\n", key, value, db_filename);

    int error = seL4_NoError;
    SQL_EXEC(kvstore_db, insert_format, key, value);
    CHECK_ERROR(error, "failed to insert pair to kvstore table");
    return error;
}

int kvstore_server_get(seL4_Word key, seL4_Word *value)
{
    KVSTORE_PRINTF("kvstore_server_get: key (%ld)\n", key);

    int error = seL4_NoError;
    sqlite3_stmt *stmt;

    SQL_MAKE_CMD(select_format, key);
    error = sqlite3_prepare_v2(kvstore_db, sql_cmd, -1, &stmt, 0);
    CHECK_ERROR(error, "failed to prepare sql cmd");

    // Execute the statement (gets one row if it exists)
    int res = sqlite3_step(stmt);
    if (res != SQLITE_ROW)
    {
        // This means there was no data found for the key
        error = sqlite3_finalize(stmt);
        CHECK_ERROR(error, "failed to finalize sql cmd");
        return KVSTORE_ERROR_KEY;
    }

    // Retrieve value
    const char *val_s = (const char *)sqlite3_column_text(stmt, 0);
    CHECK_ERROR(val_s == NULL, "failed to get value from row");

    *value = atoi(val_s);
    error = sqlite3_finalize(stmt);
    CHECK_ERROR(error, "failed to finalize sql cmd");

    return error;
}