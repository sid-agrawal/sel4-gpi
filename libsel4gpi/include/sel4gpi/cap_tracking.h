
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
    seL4_Word slot;
    gpi_cap_t type;
    bool isUntyped;
    seL4_Word paddr;

    /*OSmosis generated PD ID*/
    osmosis_pd_id_t pd_id;

    /* If this cap is a minted cap */
    bool is_minted;
    seL4_Word minted_from;
} osmosis_cap_t;

/*
    Add cap to the cap tracking object.
*/
int add_cap_data(osmosis_cap_t *new_cap_data);

/*
    Remove cap from the cap tracking object.
*/
int remove_cap_data(seL4_CPtr cap_to_remove);

/*
    Retrive cap's data from the cap tracking object.
*/
int retrive_cap_data(seL4_CPtr cap_to_find,
                     osmosis_cap_t *return_data);