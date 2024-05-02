
#pragma once

#include <stdint.h>

#include <sel4/sel4.h>
#include <utils/uthash.h>

#define CSV_MAX_STRING_SIZE (size_t)100 // Define a suitable size for your strings

/* Definition of the model state graph structure */

typedef enum _gpi_model_component_type
{
    GPI_MODEL_NODE = 1,
    GPI_MODEL_EDGE
} gpi_model_component_type_t;

typedef enum _gpi_node_type
{
    GPI_NODE_TYPE_PD,
    GPI_NODE_TYPE_RESOURCE,
    GPI_NODE_TYPE_SPACE,
} gpi_node_type_t;

typedef enum _gpi_edge_type
{
    GPI_EDGE_TYPE_SUBSET,
    GPI_EDGE_TYPE_MAP,
    GPI_EDGE_TYPE_REQUEST,
    GPI_EDGE_TYPE_HOLD,
} gpi_edge_type_t;

typedef struct _gpi_model_node
{
    gpi_node_type_t node_type;

    // Unique ID of the node (unique relative to the node type)
    // Also UTHash key
    char id[CSV_MAX_STRING_SIZE];

    // Stores any additional data in the node
    // Eg. the name of a PD node, or the type of a resource node
    char data[CSV_MAX_STRING_SIZE];

    UT_hash_handle hh;
} gpi_model_node_t;

typedef struct
{
    // (XXX) Arya: currently doesn't capture the namespace,
    // but this will be captured again when resource spaces are implemented

    gpi_edge_type_t type;
    gpi_cap_t req_type; // For request edges only
    char from[CSV_MAX_STRING_SIZE];
    char to[CSV_MAX_STRING_SIZE];
} gpi_model_edge_key_t;

typedef struct _gpi_model_edge
{
    // Data is contained in the key
    gpi_model_edge_key_t k;

    UT_hash_handle hh;
} gpi_model_edge_t;

// Generic component storage for portable model states
typedef struct _gpi_model_state_component
{
    gpi_model_component_type_t type;

    union
    {
        gpi_model_node_t node;
        gpi_model_edge_t edge;
    };
} gpi_model_state_component_t;

// Entire Model State
typedef struct
{
    // UTHash table of nodes and edges
    gpi_model_node_t *nodes;
    gpi_model_edge_t *edges;

    // Tracks free memory for structures
    // Used only for portable model states
    // Otherwise, uses malloc for structures
    gpi_model_state_component_t *mem_start;
    gpi_model_state_component_t *mem_ptr;
    gpi_model_state_component_t *mem_end;
} model_state_t;

/**
 * Initialize a new model state
 *
 * @param model_state An allocated model state structure to initialize
 * @param free_ptr Pointer to some memory to use for state structures
 *                 If provided, nodes and edges will be stored here
 *                 If NULL, will use malloc instead
 * @param free_size Size in bytes of the memory pointed to by free_ptr, if provided
 */
void init_model_state(model_state_t *model_state, void *free_ptr, size_t free_size);

// Clean up any non-portable data of the model state before sharing with another process
void clean_model_state(model_state_t *model_state);

// Frees the entire model state
void destroy_model_state(model_state_t *model_state);

// Export the model state to a buffer with CSV formatting
void export_model_state(model_state_t *model_state, char *buffer, size_t len);

// Print the model state to a terminal with CSV formatting
void print_model_state(model_state_t *model_state);

// Add any nodes and edges from source state to dest state
void combine_model_states(model_state_t *dest, model_state_t *src);

/**
 * Add a resource to the model state
 *
 * @param model_state
 * @param res_type type of the resource
 * @param res_space_id resource space the resource is from (XXX to be removed)
 * @param res_id unique ID of the resource (unique given the resource type)
 * @return The model node for the resource
 */
gpi_model_node_t *add_resource_node(model_state_t *model_state, gpi_cap_t res_type, uint64_t res_space_id, uint64_t res_id);

/**
 * Add a resource space to the model state
 *
 * @param model_state
 * @param resource_type type of the resource space
 * @param res_space_id unique ID of the resource space
 * @return The model node for the resource space
 */
gpi_model_node_t *add_resource_space_node(model_state_t *model_state, gpi_cap_t resource_type, uint64_t res_space_id);

/**
 * Add a PD to the model state
 *
 * @param model_state
 * @param pd_name the "friendly" name of the PD, may be NULL
 * @param pd_id unique ID of the PD
 * @return The model node for the PD
 */
gpi_model_node_t *add_pd_node(model_state_t *model_state, char *pd_name, uint64_t pd_id);

/**
 * Returns the root node of the model state (the root task PD)
 * Creates it if it does not exist
 */
gpi_model_node_t *get_root_node(model_state_t *model_state);

/**
 * Generate the string ID for a resource space
 * @param resource_type type of the resource space
 * @param res_space_id unique ID of the resource space
 * @param str_id returns the string ID, must be a buffer of length CSV_MAX_STRING_SIZE
 */
void get_resource_space_id(gpi_cap_t resource_type, uint64_t res_space_id, char *str_id);

/**
 * Generate the string ID for a resource node
 * @param res_type type of the resource
 * @param res_space_id resource space the resource is from (XXX to be removed)
 * @param res_id unique ID of the resource (unique given the resource type)
 * @param str_id returns the string ID, must be a buffer of length CSV_MAX_STRING_SIZE
 */
void get_resource_id(gpi_cap_t res_type, uint64_t res_space_id, uint64_t res_id, char *str_id);

/**
 * Generate the string ID for a PD node from its numeric ID
 * @param pd_id unique ID of the PD
 * @param str_id returns the string ID, must be a buffer of length CSV_MAX_STRING_SIZE
 */
void get_pd_id(uint64_t pd_id, char *str_id);

/**
 * Functions to add edges to the model state
 */

/**
 * Add a directed edge to the model state
 * @param type The edge type
 * @param from The source node of the directed edge
 * @param to The destination node of the directed edge
 */
void add_edge(model_state_t *model_state, gpi_edge_type_t type, gpi_model_node_t *from, gpi_model_node_t *to);

/**
 * Add a directed REQUEST edge to the model state
 * @param from The source node of the directed edge
 * @param to The destination node of the directed edge
 * @param req_type The type of object for the request
 */
void add_request_edge(model_state_t *model_state, gpi_model_node_t *from, gpi_model_node_t *to, gpi_cap_t req_type);