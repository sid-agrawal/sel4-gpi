
#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

/* Resource Server Debug topics
 * Clients can have topic-toggled debugging by passing in custom conditions
 * to OSDB_LVL_PRINT
 */
#define NO_DEBUG 0x0
#define PD_DEBUG 0x1
#define CPU_DEBUG 0x2
#define ADS_DEBUG 0x4
#define MO_DEBUG 0x8
#define RESSPC_DEBUG 0x10
#define GPI_DEBUG 0x20
#define FS_DEBUG 0x40 // (XXX) Arya: WIP to move remote resource server debug controls to this
#define EP_DEBUG 0x80
#define ALL_DEBUG 0xff
#define MESSAGE_DEBUG_ENABLED 0 // Separate toggle, prints all RPC messages to root task

// selectively enable component debug e.g. (PD_DEBUG | ADS_DEBUG)
#define OSDB_TOPIC (NO_DEBUG)

/** Debug levels */
#define OSDB_VERBOSE 1
#define OSDB_INFO 2
#define OSDB_WARN 3
#define OSDB_ERROR 4

/* Only messages of this level and higher will be printed */
#define OSDB_LEVEL OSDB_VERBOSE

/* Topic toggling condition for resource servers */
#define OSDB_SERVER_PRINT_ALLOWED ((OSDB_TOPIC) & (DEBUG_ID))

/*
 * Utility print, toggled by debug level and any supplied condition
 * if enabled, errors will print regardless of additional conditions
 * any other debug level is toggled by the additional condition
 */
#define OSDB_LVL_PRINT(lvl, cond, ...)                                    \
    do                                                                    \
    {                                                                     \
        if (((lvl) == OSDB_ERROR) || (((lvl) >= (OSDB_LEVEL)) && (cond))) \
        {                                                                 \
            printf(__VA_ARGS__);                                          \
        }                                                                 \
    } while (0)

// For printing within servers, requires a SERVER_ID and DEBUG_ID defined
#define OSDB_PRINT_VERBOSE(msg, ...) \
    OSDB_LVL_PRINT(OSDB_VERBOSE, OSDB_SERVER_PRINT_ALLOWED, SERVER_ID msg, ##__VA_ARGS__)
#define OSDB_PRINTF(msg, ...) OSDB_LVL_PRINT(OSDB_INFO, OSDB_SERVER_PRINT_ALLOWED, SERVER_ID msg, ##__VA_ARGS__)
#define OSDB_PRINTERR(msg, ...) OSDB_LVL_PRINT(OSDB_ERROR, OSDB_SERVER_PRINT_ALLOWED, \
                                               COLORIZE("[ERROR] " SERVER_ID, RED) msg, ##__VA_ARGS__)
#define OSDB_PRINTWARN(msg, ...) OSDB_LVL_PRINT(OSDB_WARN, OSDB_SERVER_PRINT_ALLOWED, \
                                                COLORIZE("[WARNING] " SERVER_ID, YELLOW) msg, ##__VA_ARGS__)
// prints without pre-pending SERVER_ID
#define OSDB_PRINTF_2(msg, ...) OSDB_LVL_PRINT(OSDB_INFO, OSDB_SERVER_PRINT_ALLOWED, msg, ##__VA_ARGS__)

/* For highlighting a certain print so that it's easier to see during debugging - should not remain in committed code */
#define CPRINTF(msg, ...)                                               \
    do                                                                  \
    {                                                                   \
        printf(COLORIZE("%s: ", MAGENTA) msg, __func__, ##__VA_ARGS__); \
    } while (0)
