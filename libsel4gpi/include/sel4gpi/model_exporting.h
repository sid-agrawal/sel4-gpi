
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

// Function to export the model state to a buffer with CSV formatting
void init_model_state(model_state_t *model_state);

// Function to export the model state to a buffer with CSV formatting
void export_model_state(model_state_t *model_state, char *buffer, size_t len);

// Function to print the model state to a terminal with CSV formatting
void print_model_state(model_state_t *model_state);

// Add any entries from the "from" model state to the "to" model state
void combine_model_states(model_state_t *to, model_state_t *from);

// Function to add a resource to the model state
void add_resource(model_state_t *model_state, char *resource_type, char *resource_id);

// Function to add a PD to the model state
void add_pd(model_state_t *model_state, char *pd_name, char *pd_id);

// Function to add a mapping between a PD and a resource to the model state
void add_has_access_to(model_state_t *model_state, char *pd_from, char *resource_to, bool is_mapped);

// Function to add a resource relationship to the model state
void add_resource_depends_on(model_state_t *model_state, char *resource_from, char *resource_to);

// Function to add a resource to the model state, using the given row struct
void add_resource_row(model_state_t *model_state, char *resource_type, char *resource_id, csv_row_t* row);

// Function to add a mapping between a PD and a resource to the model state, using the given row struct
void add_has_access_to_row(model_state_t *model_state, char *pd_from, char *resource_to, bool is_mapped, csv_row_t* row);

// Function to add a resource relationship to the model state, using the given row struct
void add_resource_depends_on_row(model_state_t *model_state, char *resource_from, char *resource_to, csv_row_t* row);

// Function add a PD to PD relationship to the model state
void add_pd_requestes(model_state_t *model_state, char *pd_from, char *pd_to);