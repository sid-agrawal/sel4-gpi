/**
 * @file gpi_server.h
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief API for a parent to spawn a GPI server.
 * @version 0.1
 * @date 2022-04-05
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <vka/vka.h>
#include <vspace/vspace.h>
#include <sel4utils/process.h>
#include <sel4gpi/ads_server.h>
#include <sel4gpi/cpu_server.h>


#define GPI_SERVER_DEFAULT_PRIORITY    (seL4_MaxPrio - 1)

#define GPISERVP     "GPIServ Parent: "
#define GPISERVS     "GPIServ Server: "


enum GPICAP_TYPE {
    GPICAP_TYPE_NONE = 0,
    GPICAP_TYPE_ADS,
    GPICAP_TYPE_CPU,
    GPICAP_TYPE_MAX,
};

#define  GPI_SERVER_BADGE_PARENT_VALUE 0xdeadbeef// Change this to something which will not violate the badge range

/** @file API for allowing a thread to act as the parent to a GPI server
 * thread.
 *
 * Provides the APIs for spawning the server thread.
 */

/** Spawns the server thread. Server thread is spawned within the VSpace and
 *  CSpace of the thread that spawned it.
 *
 * CAUTION:
 * All vka_t, vpsace_t, and simple_t instances passed to this library by
 * reference must remain functional throughout the lifetime of the server.
 *
 * @param parent_simple Initialized simple_t for the parent process that is
 *                      spawning the server thread.
 * @param parent_vka Initialized vka_t for the parent process that is spawning
 *                   the server thread.
 * @param parent_vspace Initialized vspace_t for the parent process that is
 *                      spawning the server thread.
 * @param priority Server thread's priority.
 * @param server_endpoint Server thread's endpoint cap.
 * @return seL4_Error value.
 */
seL4_Error gpi_server_parent_spawn_thread(simple_t *parent_simple,
                                          vka_t *parent_vka,
                                          vspace_t *parent_vspace,
                                          uint8_t priority,
                                          seL4_CPtr *server_endpoint);


/* 
Context of the server
*/
typedef struct _gpi_server_context {
    simple_t *server_simple;
    vka_t *server_vka;
    seL4_CPtr server_cspace;
    vspace_t *server_vspace;
    sel4utils_thread_t server_thread;

    // The server listens on this endpoint. 
    vka_object_t server_ep_obj;

    // Parent's badge value.
    // There is only 1 parent and hence only 1 badge value.
    seL4_Word parent_badge_value;
    cspacepath_t _badged_server_ep_cspath;

    /* Per-client context maintained by the server. */
    ads_component_context_t ads_server;
    cpu_component_context_t cpu_server;
} gpi_server_context_t;

/** 
 * Internal library function: acts as the main() for the server thread.
 **/
void gpi_server_main(void);

gpi_server_context_t *get_gpi_server(void);

/*
 How we are using the badge.
 There are a total of 64 bits.
63:56  8 bits for the type of cap.
55:40 16 bits for permissions, as a bit mask.
39:20 20 bits for client ID
19:0  20 bits for object ID 
*/


// Bits: 63:56 are for the cap type. Total of 8 bits, so 255 types.
inline uint64_t get_cap_type_from_badge(seL4_Word badge)
{
    return (badge >> 56) & 0xFF;
}

// Bits: 63:56 are for the cap type. Total of 8 bits, so 255 types.
inline seL4_Word set_cap_type_to_badge(seL4_Word badge, uint64_t type)
{
    assert(type <= 0xFF);
    return (badge & 0x00FFFFFFFFFFFFFF) | (type << 56);
}

// Bits: 55:40 are for the permisions. Total of 16 bits, as a bit-mask so 16 permissions.
inline uint64_t get_perms_from_badge(seL4_Word badge)
{
    return (badge >> 40) & 0xFFFF;
}

// Bits: 55:40 are for the permisions. Total of 16 bits, as a bit-mask so 16 permissions.
inline uint64_t set_perms_to_badge(seL4_Word badge, uint64_t perms)
{
    assert(perms <= 0xFFFF);
    return (badge & 0xFF0000FFFFFFFFFF) | (perms << 40);
}

// Bits: 39:20 are for the client id. Total of 20 bits, so 2^20 clients.
inline uint64_t get_client_id_from_badge(seL4_Word badge)
{
    return (badge >> 20) & 0xFFFFF;
}

// Bits: 39:20 are for the client id. Total of 20 bits, so 2^20 clients.
inline uint64_t set_client_id_to_badge(seL4_Word badge, uint64_t client_id)
{
    assert(client_id <= 0xFFFFF);
    return (badge & 0xFFFFFF00000FFFFF) | (client_id << 20);
}

// Bits: 19:0 are for the object id. Total of 32 bits, so 2^20 objects.
inline uint64_t get_object_id_type_from_badge(seL4_Word badge)
{
    return (badge & 0xFFFFF);
}

// Bits: 31:0 are for the object id. Total of 32 bits, so 2^32 objects.
inline uint64_t set_object_id_type_from_badge(seL4_Word badge, uint64_t object_id)
{
    assert(object_id <= 0xFFFFF);
    return (badge & 0xFFFFFFFFFFF00000) | object_id;
}