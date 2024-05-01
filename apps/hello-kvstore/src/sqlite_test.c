#include <autoconf.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <assert.h>

#include <sel4/sel4.h>

#include <sqlite3/sqlite3.h>

#define PRINT_CALLBACK 0
#define DB_NAME_FORMAT "/test-db-%d.db"
#define T1_NAME "t1"
#define T2_NAME "t2"
#define N_INSERT 50
#define CMDLEN 128

char db_name[128];

#define SQL_EXEC_SELECT(sql_db, format, ...)                                       \
    do                                                                             \
    {                                                                              \
        sqlite_row *listptr = list_head;                                           \
        listptr->nvals = 0;                                                        \
        error = snprintf(sql_cmd, CMDLEN, format, __VA_ARGS__);                    \
        assert(error != -1);                                                       \
        error = sqlite3_exec(sql_db, sql_cmd, sqlite_callback, &listptr, &errmsg); \
        print_error(error, errmsg, db);                                            \
        assert(error == SQLITE_OK);                                                \
    } while (0);

#define SQL_EXEC(sql_db, format, ...)                                          \
    do                                                                         \
    {                                                                          \
        error = snprintf(sql_cmd, CMDLEN, format, __VA_ARGS__);                \
        assert(error != -1);                                                   \
        error = sqlite3_exec(sql_db, sql_cmd, sqlite_callback, NULL, &errmsg); \
        print_error(error, errmsg, db);                                        \
        assert(error == SQLITE_OK);                                            \
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

int sqlite_tests(void)
{
    printf("---- Begin SQLite tests ----\n");
    int error;
    char *errmsg = 0;
    char sql_cmd[CMDLEN];

    // Load an initial db
    sqlite3 *db;

    // Generate a random DB name

    /* Try a few filenames in case there is already a test db */
    for (int i = 0; i < 10; i++)
    {
        error = snprintf(db_name, 128, DB_NAME_FORMAT, i);
        assert(error != -1);

        // Check if the file exists
        if (access(db_name, F_OK))
        {
            printf("sqlite_tests: DB %s already exists, trying another name\n", db_name);
        }
        else
        {
            break;
        }
    }

    error = sqlite3_open(db_name, &db);
    assert(error == SQLITE_OK);
    assert(db != NULL);

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
    assert(list_head->nvals == N_INSERT);
    assert(list_head->next->nvals == 2);
    assert(strcmp(list_head->next->vals[0], "string-0") == 0);
    free_sqlite_row_list();

    const char *sql_select_where_format = "select one from %s where two >= %d and two < %d";
    SQL_EXEC_SELECT(db, sql_select_where_format, T1_NAME, 10, 20);
    assert(list_head->nvals == 10);
    assert(list_head->next->nvals == 1);
    assert(strcmp(list_head->next->vals[0], "string-10") == 0);
    free_sqlite_row_list();

    // Try an update
    const char *sql_update_format = "update %s set one = '%s' where two = %d";
    SQL_EXEC(db, sql_update_format, T1_NAME, "lucky", 7);
    SQL_EXEC_SELECT(db, sql_select_where_format, T1_NAME, 7, 8);
    assert(list_head->nvals == 1);
    assert(strcmp(list_head->next->vals[0], "lucky") == 0);
    free_sqlite_row_list();

    // Try a delete
    const char *sql_delete_format = "delete from %s where two >= %d and two < %d";
    SQL_EXEC(db, sql_delete_format, T1_NAME, 10, 20);
    SQL_EXEC_SELECT(db, sql_select_where_format, T1_NAME, 10, 30);
    assert(list_head->nvals == 10);
    assert(strcmp(list_head->next->vals[0], "string-20") == 0);
    free_sqlite_row_list();

    /* Don't close so we can see the files in model state */

    // Close the dbs
    error = sqlite3_close(db);
    assert(error == SQLITE_OK);

    // Shut down sqlite
    error = sqlite3_shutdown();
    assert(error == SQLITE_OK);

    printf("---- SQLite tests pass ----\n");

exit:
    return error;
}