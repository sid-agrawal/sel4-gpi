
#pragma once

#include <stdint.h>

#include <sel4/sel4.h>
#include <sel4gpi/badge_usage.h>

typedef struct osmosis_pd_id {
    seL4_Word l0_pd_id;
    seL4_Word l1_pd_id;
    seL4_Word l3_pd_id;
} osmosis_pd_id_t;





typedef struct osmosis_cap {
    // The type of the cap as per seL4
    // Sid is unsure if we need both.
    seL4_Word slot_in_rt;

    seL4_Word slot_in_pd;

    gpi_cap_t type;
    bool isUntyped;
    seL4_Word paddr;

    /* If this cap is a minted cap */
    bool isMinted;
    seL4_Word minted_from;
} osmosis_cap_t;


void print_osm_cap_info (osmosis_cap_t *o);

/*
    Add cap to the cap tracking object.
*/
int gpi_add_cap_data(osmosis_cap_t *new_cap_data);

/*
    Remove cap from the cap tracking object.
*/
int gpi_remove_cap_data(seL4_CPtr cap_to_remove);

/*
    Retrieve cap's data from the cap tracking object.
*/
int gpi_retrieve_cap_data(seL4_CPtr cap_to_find,
                     osmosis_cap_t *return_data);