
#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#define CSV_MAX_STRING_SIZE (size_t)100 // Define a suitable size for your strings

// Struct to represent a row in the CSV
typedef struct csv_row
{
    char resource_from[CSV_MAX_STRING_SIZE];
    char resource_to[CSV_MAX_STRING_SIZE];
    char resource_type[CSV_MAX_STRING_SIZE];
    char resource_id[CSV_MAX_STRING_SIZE];
    char pd_name[CSV_MAX_STRING_SIZE];
    char pd_from[CSV_MAX_STRING_SIZE];
    char pd_to[CSV_MAX_STRING_SIZE];
    char pd_id[CSV_MAX_STRING_SIZE];
    char is_mapped[CSV_MAX_STRING_SIZE];
    char constraints[CSV_MAX_STRING_SIZE];

    struct csv_row *next;
} csv_row_t;

// Entire Model State
typedef struct
{
    csv_row_t *csv_rows;
    int csv_rows_len;
    int num_pds;
    int num_resources;

    // Types of edges
    int num_depends_on;
    int num_has_access_to;
    int num_requests;
} model_state_t;

// Struct to represent a row in the CSV
// Shortened version for resource relations only
typedef struct csv_rr_row
{
    char resource_from[CSV_MAX_STRING_SIZE];
    char resource_to[CSV_MAX_STRING_SIZE];
    char resource_type[CSV_MAX_STRING_SIZE];
    char resource_id[CSV_MAX_STRING_SIZE];
} csv_rr_row_t;

// Resource Relation State
// Used when a remote resource server sends
// resource relations
typedef struct
{
    csv_rr_row_t *csv_rows;
    int csv_rows_len;
    int num_resources;

    // Types of edges
    int num_depends_on;
} rr_state_t;

// Initialize a new model state
void init_model_state(model_state_t *model_state);

// Initialize a new resource relation state
void init_rr_state(rr_state_t *model_state);

// Function to export the model state to a buffer with CSV formatting
void export_model_state(model_state_t *model_state, char *buffer, size_t len);

// Function to print the model state to a terminal with CSV formatting
void print_model_state(model_state_t *model_state);

// Add any resource relations from a rr_state_t to the model state
void combine_model_states(model_state_t *ms, rr_state_t *rs);

// Write a string resource ID from cap type and integer ID
// Requires that the length of res_id is CSV_MAX_STRING_SIZE
void make_res_id(char *res_id, gpi_cap_t cap_type, uint64_t res_id_int);

void make_virtual_res_id(char *res_id, uint32_t obj_id, uint64_t res_id_int, char *prefix);

void make_phys_res_id(char *res_id, uint32_t obj_id, uint64_t res_id_int, char *prefix);

// Function to add a resource to the model state
void add_resource(model_state_t *model_state, char *resource_type, char *resource_id);

// Function to add a resource to the model state with a comment
void add_resource_2(model_state_t *model_state, char *resource_type, char *resource_id, char *comment);

// Function to add a PD to the model state
void add_pd(model_state_t *model_state, char *pd_name, char *pd_id);

// Function to add a mapping between a PD and a resource to the model state
void add_has_access_to(model_state_t *model_state, char *pd_from, char *resource_to, bool is_mapped);

// Function to add a resource relationship to the model state
void add_resource_depends_on(model_state_t *model_state, char *resource_from, char *resource_to);

// Function to add a resource to the rr state
void add_resource_rr(rr_state_t *model_state, gpi_cap_t resource_type, char *resource_id, csv_rr_row_t *new_row);

// Function to add a resource relationship to the rr state
void add_resource_depends_on_rr(rr_state_t *model_state, char *resource_from, char *resource_to, csv_rr_row_t *new_row);

// Function add a PD to PD relationship to the model state
void add_pd_requests(model_state_t *model_state, char *pd_from, char *pd_to, gpi_cap_t type, char *constraints);