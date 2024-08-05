#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <sel4test/test.h>
#include <sel4test/macros.h>
#include "../test.h"
#include "../helpers.h"
#include "test_shared.h"
#include <vka/capops.h>

#include <sel4gpi/pd_utils.h>

#include <ramdisk_client.h>
#include <fs_client.h>
#include <sqlite3/sqlite3.h>

#define PRINT_CALLBACK 0
#define DB_NAME "/test-db.db"
#define DB_2_NAME "/test-db2.db"
#define T1_NAME "t1"
#define T2_NAME "t2"
#define N_INSERT 50
#define CMDLEN 128

#define SQL_EXEC_SELECT(sql_db, format, ...)                                       \
    do                                                                             \
    {                                                                              \
        sqlite_row *listptr = list_head;                                           \
        listptr->nvals = 0;                                                        \
        error = snprintf(sql_cmd, CMDLEN, format, __VA_ARGS__);                    \
        test_assert(error != -1);                                                  \
        error = sqlite3_exec(sql_db, sql_cmd, sqlite_callback, &listptr, &errmsg); \
        print_error(error, errmsg, db);                                            \
        test_assert(error == SQLITE_OK);                                           \
    } while (0);

#define SQL_EXEC(sql_db, format, ...)                                          \
    do                                                                         \
    {                                                                          \
        error = snprintf(sql_cmd, CMDLEN, format, __VA_ARGS__);                \
        test_assert(error != -1);                                              \
        error = sqlite3_exec(sql_db, sql_cmd, sqlite_callback, NULL, &errmsg); \
        print_error(error, errmsg, db);                                        \
        test_assert(error == SQLITE_OK);                                       \
    } while (0);

// Structures store the results of a sql select statement for verification
typedef struct sqlite_row sqlite_row;
struct sqlite_row
{
    int nvals;
    char **vals;
    sqlite_row *next;
};

sqlite_row list_head_node;
sqlite_row *list_head = &list_head_node; // Node at the beginning of a result list

/**
 * Callback for executed SQL statements
 * Prints the results if PRINT_CALLBACK != 0
 * Stores results to a linked list if list != NULL
 */
static int sqlite_callback(void *listp, int argc, char **argv, char **azColName)
{
    int i;

    sqlite_row *newrow;

    if (listp != NULL)
    {
        // Create new node for this row
        newrow = malloc(sizeof(sqlite_row));
        newrow->nvals = argc;
        newrow->vals = malloc(argc * sizeof(char *));
        newrow->next = NULL;

        // Point old node to new node
        sqlite_row *lastrow = *((sqlite_row **)listp);
        lastrow->next = newrow;
        *((sqlite_row **)listp) = newrow;

        // Update the total row count
        list_head->nvals++;
    }

    // Record the row's results
    for (i = 0; i < argc; i++)
    {
        if (PRINT_CALLBACK)
        {
            printf("%s = %s; ", azColName[i], argv[i] ? argv[i] : "NULL");
        }

        if (newrow != NULL)
        {
            if (argv[i] != NULL)
            {
                newrow->vals[i] = malloc(strlen(argv[i]) + 1);
                strcpy(newrow->vals[i], argv[i]);
            }
        }
    }

    if (PRINT_CALLBACK)
    {
        printf("\n");
    }

    return 0;
}

// Frees the linked list of rows from a previous call to SQL_EXEC_SELECT
static void free_sqlite_row_list()
{
    sqlite_row *curr = list_head->next;
    sqlite_row *next;

    int j = 0;
    while (curr != NULL)
    {
        next = curr->next;

        for (int i = 0; i < curr->nvals; i++)
        {
            if (curr->vals[i] != NULL)
            {
                free(curr->vals[i]);
            }
        }
        free(curr->vals);
        free(curr);

        j++;
        curr = next;
    }

    list_head->nvals = 0;
    list_head->next = NULL;
}

/**
 * If there is an error, display details
 */
static void print_error(int error, char *errmsg, sqlite3 *db)
{
    if (error != SQLITE_OK)
    {
        printf("SQL error: %s\n", errmsg);
        sqlite3_free(errmsg);
        printf("sqlite3_exec error, extended errcode: %d\n", sqlite3_extended_errcode(db));
    }
}

static void debug_print_file(char *filename)
{
    char *buf = malloc(256);
    int fd = open(filename, O_RDONLY);
    int nbytes = read(fd, buf, 256);

    printf("------------FILE START--------------\n");
    for (int i = 0; i < nbytes; i++)
    {
        printf("%x", buf[i] & 0xff);
    }
    printf("\n------------FILE END--------------\n");

    free(buf);
}

int test_sqlite(env_t env)
{
    int error;
    char *errmsg = 0;
    char sql_cmd[CMDLEN];

    printf("------------------STARTING SETUP: %s------------------\n", __func__);

    /* Initialize the PD */
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();

    /* Start ramdisk server process */
    gpi_space_id_t ramdisk_id;
    seL4_CPtr ramdisk_pd_cap;
    error = start_ramdisk_pd(&ramdisk_pd_cap, &ramdisk_id);
    test_assert(error == 0);

    /* Start fs server process */
    gpi_space_id_t fs_id;
    seL4_CPtr fs_pd_cap;
    error = start_xv6fs_pd(ramdisk_id, &fs_pd_cap, &fs_id);
    test_assert(error == 0);

    // Add FS ep to RDE
    seL4_CPtr fs_client_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME));

    printf("------------------STARTING TESTS: %s------------------\n", __func__);

    // The libc fs ops should go to the xv6fs server
    xv6fs_client_init();

    // Load an initial db
    sqlite3 *db;
    error = sqlite3_open(DB_NAME, &db);
    test_assert(error == SQLITE_OK);
    test_assert(db != NULL);

    // Create some tables
    const char *sql_create_format = "create table %s(one varchar(10), two smallint);";
    SQL_EXEC(db, sql_create_format, T1_NAME);
    SQL_EXEC(db, sql_create_format, T2_NAME);

    // Insert values
    const char *sql_insert_format = "insert into %s values('string-%d',%d);";

    for (int i = 0; i < N_INSERT; i++)
    {
        SQL_EXEC(db, sql_insert_format, T1_NAME, i, i);
        SQL_EXEC(db, sql_insert_format, T2_NAME, i, i);
    }

    // Try some select queries
    const char *sql_select_all_format = "select * from %s";
    SQL_EXEC_SELECT(db, sql_select_all_format, T1_NAME);
    test_assert(list_head->nvals == N_INSERT);
    test_assert(list_head->next->nvals == 2);
    test_assert(strcmp(list_head->next->vals[0], "string-0") == 0);
    free_sqlite_row_list();

    const char *sql_select_where_format = "select one from %s where two >= %d and two < %d";
    SQL_EXEC_SELECT(db, sql_select_where_format, T1_NAME, 10, 20);
    test_assert(list_head->nvals == 10);
    test_assert(list_head->next->nvals == 1);
    test_assert(strcmp(list_head->next->vals[0], "string-10") == 0);
    free_sqlite_row_list();

    // Try an update
    const char *sql_update_format = "update %s set one = '%s' where two = %d";
    SQL_EXEC(db, sql_update_format, T1_NAME, "lucky", 7);
    SQL_EXEC_SELECT(db, sql_select_where_format, T1_NAME, 7, 8);
    test_assert(list_head->nvals == 1);
    test_assert(strcmp(list_head->next->vals[0], "lucky") == 0);
    free_sqlite_row_list();

    // Try a delete
    const char *sql_delete_format = "delete from %s where two >= %d and two < %d";
    SQL_EXEC(db, sql_delete_format, T1_NAME, 10, 20);
    SQL_EXEC_SELECT(db, sql_select_where_format, T1_NAME, 10, 30);
    test_assert(list_head->nvals == 10);
    test_assert(strcmp(list_head->next->vals[0], "string-20") == 0);
    free_sqlite_row_list();

    // Try opening a second db
    sqlite3 *db2;
    error = sqlite3_open(DB_2_NAME, &db2);
    test_assert(error == SQLITE_OK);
    test_assert(db2 != NULL);

    SQL_EXEC(db2, sql_create_format, T1_NAME);
    SQL_EXEC(db2, sql_insert_format, T1_NAME, 100, 100);
    SQL_EXEC_SELECT(db2, sql_select_all_format, T1_NAME);
    test_assert(list_head->nvals == 1);
    test_assert(strcmp(list_head->next->vals[0], "string-100") == 0);
    free_sqlite_row_list();

    // Close the dbs
    error = sqlite3_close(db);
    test_assert(error == SQLITE_OK);

    error = sqlite3_close(db2);
    test_assert(error == SQLITE_OK);

    // Print model state
    extract_model(&pd_conn);

    // Shut down sqlite
    error = sqlite3_shutdown();
    test_assert(error == SQLITE_OK);

    /* Remove RDEs from test process so that it won't be cleaned up by recursive cleanup */
    error = pd_client_remove_rde(&pd_conn, sel4gpi_get_resource_type_code(BLOCK_RESOURCE_TYPE_NAME), BADGE_SPACE_ID_NULL);
    test_assert(error == 0);

    error = pd_client_remove_rde(&pd_conn, sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME), BADGE_SPACE_ID_NULL);
    test_assert(error == 0);

    // Cleanup servers
    pd_client_context_t fs_pd_conn;
    fs_pd_conn.ep = fs_pd_cap;
    test_error_eq(maybe_terminate_pd(&fs_pd_conn), 0);

    pd_client_context_t ramdisk_pd_conn;
    ramdisk_pd_conn.ep = ramdisk_pd_cap;
    test_error_eq(maybe_terminate_pd(&ramdisk_pd_conn), 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPISQ001, "Ensure that sqlite can run", test_sqlite, true)