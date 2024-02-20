/**
 * @file cap_tracking.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the methods to manipulate the cap's metadata object
 * @version 0.1
 * @date 2023-11-13
 *
 * @copyright Copyright (c) 2023
 *
 */

#include <sel4utils/process.h>
#include <sel4utils/vspace.h>

#include <sel4gpi/pd_component.h>
#include <sel4gpi/pd_obj.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/gpi_server.h>
#include <sel4gpi/cap_tracking.h>


void print_osm_cap_info (osmosis_cap_t *o) {
    printf("Slot_RT:%lx\t T: %s \t %s \t Paddr: 0x%lx \t %s \t Minted from: %lx\n",
            o->slot_in_rt,
           cap_type_to_str(o->type),
           o->isUntyped ? "Untyped" : "Not Untyped",
           o->paddr,
           o->isMinted ? "Minted" : "Not Minted",
           o->minted_from);
}

/*
    returns an unintialized osmosis cap tracking object
*/
static osmosis_cap_t *new_osm_cap(void) {
    gpi_server_context_t *env = get_gpi_server();
    osmosis_cap_t *new = calloc(1, sizeof(osmosis_cap_t));
    if (env->osm_caps == NULL) {
        env->osm_caps = new;
    }

    if (env->osm_caps_tail) {
        new->prev = env->osm_caps_tail;
        env->osm_caps_tail->next = new;
    }
    
    env->osm_caps_tail = new;
    return new;
} 

/*
    Add cap to the cap tracking object.
*/
int gpi_add_cap_data(osmosis_cap_t *new_cap_data)
{
    osmosis_cap_t *new = new_osm_cap();

    new->slot_in_rt = new_cap_data->slot_in_rt;
    new->type = new_cap_data->type;
    new->isUntyped = new_cap_data->isUntyped;
    new->paddr = new_cap_data->paddr;
    new->isMinted = new_cap_data->isMinted;
    new->minted_from = new_cap_data->minted_from;

    return 0;
}

/* Removes a cap from the cap tracking list, and frees it */
static void remove_osm_cap(osmosis_cap_t *to_remove) {
    if (to_remove->prev != NULL) {
        to_remove->prev->next = to_remove->next;
    }

    if (to_remove->next != NULL) {
        to_remove->next->prev = to_remove->prev;
    }

    free(to_remove);
}

/*
    Remove cap from the cap tracking object.
*/
int gpi_remove_cap_data(seL4_CPtr cap_to_remove)
{

    gpi_server_context_t *env = get_gpi_server();
    osmosis_cap_t *curr = env->osm_caps;
    osmosis_cap_t *next;
    bool found_cap = false;

    while (curr != NULL) {
        /* remove the cap and any of its children */
        if (curr->slot_in_rt == cap_to_remove || curr->minted_from == cap_to_remove) {
            next = curr->next; /* retain a pointer to the next node, as the current one is about to be freed */
            remove_osm_cap(curr);
            curr = next;
            found_cap = true; // could we run into a case where we somehow track a child cap without tracking its parent?
        } else {
            curr = curr->next;
        }
    }

    if (!found_cap) {
        ZF_LOGE("Could not find cap in cap tracking object");
        return 1;
    }

    return 0;
}

/*
    Retrive cap's data from the cap tracking object.
*/
int gpi_retrieve_cap_data(seL4_CPtr cap_to_find,
                     osmosis_cap_t *return_data)
{
    gpi_server_context_t *env = get_gpi_server();
    osmosis_cap_t *curr = env->osm_caps;

    while (curr != NULL) {
        if (curr->slot_in_rt == cap_to_find) {
            /* Check that a type is set */
            assert(curr->type != GPICAP_TYPE_MAX);

            /* Initilize the slot field*/
            return_data->slot_in_rt = curr->slot_in_rt;

            /* Retrive the actual data */
            return_data->type = curr->type;
            return_data->isUntyped = curr->isUntyped;
            return_data->paddr = curr->paddr;
            return_data->isMinted = curr->isMinted;
            return_data->minted_from = curr->minted_from;
            // ZF_LOGE("Found cap in cap tracking object for cap %lu, type: %u\n", cap_to_find, seL4_DebugCapIdentify(cap_to_find));
            return 0;
        }
        curr = curr->next;
    }

    ZF_LOGE("Could not find cap in cap tracking object for cap %lu, type: %u\n", cap_to_find, seL4_DebugCapIdentify(cap_to_find));
    return 1;
}
