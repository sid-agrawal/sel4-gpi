/**
 * @file model_exporting.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the methods to export the model state as a CSV
 * @version 0.1
 * @date 2023-12-27
 *
 * @copyright Copyright (c) 2023
 *
 */

#include <sel4utils/process.h>
#include <sel4utils/vspace.h>
#include <sel4utils/util.h>
#include <sel4utils/helpers.h>

#include <sel4gpi/debug.h>
#include <sel4gpi/model_exporting.h>

static char *edge_type_to_str(gpi_edge_type_t edge_type)
{
    switch (edge_type)
    {
    case GPI_EDGE_TYPE_HOLD:
        return "HOLD";
    case GPI_EDGE_TYPE_MAP:
        return "MAP";
    case GPI_EDGE_TYPE_SUBSET:
        return "SUBSET";
    case GPI_EDGE_TYPE_REQUEST:
        return "REQUEST";
    default:
        return "UNKNOWN";
    }
}

static char *node_type_to_str(gpi_node_type_t node_type)
{
    switch (node_type)
    {
    case GPI_NODE_TYPE_PD:
        return "PD";
    case GPI_NODE_TYPE_RESOURCE:
        return "RESOURCE";
    case GPI_NODE_TYPE_SPACE:
        return "RESOURCE SPACE";
    default:
        return "UNKNOWN";
    }
}

// Create a string ID for a node
static void make_node_id(char *str_id, char *prefix, uint64_t id)
{
    snprintf(str_id, CSV_MAX_STRING_SIZE, "%s_%lx", prefix, id);
}

// Create a string ID for a node with two IDs
static void make_node_id_2(char *str_id, char *prefix, uint64_t id1, uint64_t id2)
{
    snprintf(str_id, CSV_MAX_STRING_SIZE, "%s_%lx_%lx", prefix, id1, id2);
}

void init_model_state(model_state_t *model_state, void *free_ptr, size_t free_size)
{
    assert(model_state != NULL);

    model_state->nodes = NULL;
    model_state->edges = NULL;
    model_state->mem_start = (gpi_model_state_component_t *)free_ptr;
    model_state->mem_ptr = model_state->mem_start;
    model_state->mem_end = (gpi_model_state_component_t *)(free_ptr + free_size);

    if (free_ptr != NULL)
    {
        memset(free_ptr, 0, free_size);
    }
}

void clean_model_state(model_state_t *model_state)
{
    HASH_CLEAR(hh, model_state->nodes);
    HASH_CLEAR(hh, model_state->edges);
}

void destroy_model_state(model_state_t *model_state)
{
    // Delete all nodes
    gpi_model_node_t *current_node, *tmp;
    HASH_ITER(hh, model_state->nodes, current_node, tmp)
    {
        HASH_DEL(model_state->nodes, current_node);
        free(current_node);
    }

    // Delete all edges
    gpi_model_edge_t *current_edge, *tmp2;
    HASH_ITER(hh, model_state->edges, current_edge, tmp2)
    {
        HASH_DEL(model_state->edges, current_edge);
        free(current_edge);
    }

    // Free the model state
    free(model_state);
}

// Function to export the model state to a buffer with CSV formatting
void export_model_state(model_state_t *model_state, char *buffer, size_t buf_len)
{

    assert(model_state != NULL);

    size_t buf_written_total = 0;
    uint8_t width = 0;

    // Print the headers
    size_t buf_written = snprintf(buffer, buf_len - buf_written_total,
                                  "%-*s,%-*s,%-*s,%-*s,%-*s,%-*s\n",
                                  width,
                                  "NODE TYPE",
                                  width,
                                  "NODE ID",
                                  width,
                                  "DATA",
                                  width,
                                  "EDGE TYPE",
                                  width,
                                  "EDGE FROM",
                                  width,
                                  "EDGE TO");

    buffer += buf_written;
    buf_written_total += buf_written;

    // Print the nodes
    for (gpi_model_node_t *node = model_state->nodes; node != NULL; node = node->hh.next)
    {
        size_t buf_written = snprintf(buffer, buf_len - buf_written_total,
                                      "%-*s,%-*s,%-*s,%-*s,%-*s,%-*s\n",
                                      width,
                                      node_type_to_str(node->node_type),
                                      width,
                                      node->id,
                                      width,
                                      node->data,
                                      width,
                                      "",
                                      width,
                                      "",
                                      width,
                                      "");

        buffer += buf_written;
        buf_written_total += buf_written;

        assert(buf_written_total < buf_len);
    }

    // Print the edges
    for (gpi_model_edge_t *edge = model_state->edges; edge != NULL; edge = edge->hh.next)
    {
        size_t buf_written = snprintf(buffer, buf_len - buf_written_total,
                                      "%-*s,%-*s,%-*s,%-*s,%-*s,%-*s\n",
                                      width,
                                      "",
                                      width,
                                      "",
                                      width,
                                      cap_type_to_str(edge->k.req_type),
                                      width,
                                      edge_type_to_str(edge->k.type),
                                      width,
                                      edge->k.from,
                                      width,
                                      edge->k.to);

        buffer += buf_written;
        buf_written_total += buf_written;

        assert(buf_written_total < buf_len);
    }
}

// Function to export the model state to a buffer with CSV formatting
void print_model_state(model_state_t *model_state)
{

    assert(model_state != NULL);
    uint8_t width = 0;

    // Print the headers
    printf("%-*s,%-*s,%-*s,%-*s,%-*s,%-*s\n",
           width,
           "NODE TYPE",
           width,
           "NODE ID",
           width,
           "DATA",
           width,
           "EDGE TYPE",
           width,
           "EDGE FROM",
           width,
           "EDGE TO");

    // Print the nodes
    for (gpi_model_node_t *node = model_state->nodes; node != NULL; node = node->hh.next)
    {
        printf("%-*s,%-*s,%-*s,%-*s,%-*s,%-*s\n",
               width,
               node_type_to_str(node->node_type),
               width,
               node->id,
               width,
               node->data,
               width,
               "",
               width,
               "",
               width,
               "");
    }

    // Print the edges
    for (gpi_model_edge_t *edge = model_state->edges; edge != NULL; edge = edge->hh.next)
    {
        printf("%-*s,%-*s,%-*s,%-*s,%-*s,%-*s\n",
               width,
               "",
               width,
               "",
               width,
               cap_type_to_str(edge->k.req_type),
               width,
               edge_type_to_str(edge->k.type),
               width,
               edge->k.from,
               width,
               edge->k.to);
    }
}

// Add a node to the model state
static gpi_model_node_t *add_node(model_state_t *model_state, gpi_node_type_t node_type, char *id, char *data)
{
    gpi_model_node_t *node;

    // HASH_FIND(hh, model_state->nodes, id, CSV_MAX_STRING_SIZE, node);
    HASH_FIND_STR(model_state->nodes, id, node);

    if (node != NULL)
    {
        // This node already exists, don't duplicate it

        // Do overwrite data if it was empty before
        if ((strlen(node->data) == 0) && (data != NULL))
        {
            strncpy(node->data, data, CSV_MAX_STRING_SIZE);
        }

        return node;
    }
    if (model_state->mem_start == NULL)
    {
        // No free pointer, allocate a node from heap
        node = calloc(1, sizeof(gpi_model_node_t));
    }
    else
    {
        assert(model_state->mem_ptr < model_state->mem_end);

        // Allocate a node from free pointer
        gpi_model_state_component_t *component = model_state->mem_ptr;

        component->type = GPI_MODEL_NODE;
        node = &component->node;
        model_state->mem_ptr++;
    }

    assert(node != NULL);
    node->node_type = node_type;
    memset(node->id, 0, CSV_MAX_STRING_SIZE);
    strncpy(node->id, id, CSV_MAX_STRING_SIZE);

    if (data != NULL)
    {
        strncpy(node->data, data, CSV_MAX_STRING_SIZE);
    }

    // HASH_ADD_KEYPTR(hh, model_state->nodes, &node->id, CSV_MAX_STRING_SIZE, node);
    HASH_ADD_STR(model_state->nodes, id, node);

    return node;
}

static void add_edge_by_id(model_state_t *model_state, gpi_edge_type_t type, char *from_id,
                           char *to_id, gpi_cap_t req_type)
{
    gpi_model_edge_t edge_static;
    memset(&edge_static, 0, sizeof(gpi_model_edge_t));
    edge_static.k.type = type;
    edge_static.k.req_type = req_type;
    strcpy(edge_static.k.from, from_id);
    strcpy(edge_static.k.to, to_id);

    gpi_model_edge_t *edge;
    HASH_FIND(hh, model_state->edges, &edge_static.k, sizeof(gpi_model_edge_key_t), edge);

    if (edge != NULL)
    {
        // This edge already exists, don't duplicate it
        return;
    }

    if (model_state->mem_start == NULL)
    {
        // No free pointer, allocate an edge from heap
        edge = calloc(1, sizeof(gpi_model_edge_t));
    }
    else
    {
        assert(model_state->mem_ptr < model_state->mem_end);

        // Allocate a node from free pointer
        gpi_model_state_component_t *component = model_state->mem_ptr;

        component->type = GPI_MODEL_EDGE;
        edge = &component->edge;
        model_state->mem_ptr++;
    }

    assert(edge != NULL);
    *edge = edge_static;

    HASH_ADD_KEYPTR(hh, model_state->edges, &edge->k, sizeof(gpi_model_edge_key_t), edge);
}

// Generic edge function
static void add_edge_private(model_state_t *model_state, gpi_edge_type_t type, gpi_model_node_t *from,
                             gpi_model_node_t *to, gpi_cap_t req_type)
{
    add_edge_by_id(model_state, type, from->id, to->id, req_type);
}

void add_edge(model_state_t *model_state, gpi_edge_type_t type, gpi_model_node_t *from, gpi_model_node_t *to)
{
    add_edge_private(model_state, type, from, to, GPICAP_TYPE_NONE);
}

void add_request_edge(model_state_t *model_state, gpi_model_node_t *from, gpi_model_node_t *to, gpi_cap_t req_type)
{
    add_edge_private(model_state, GPI_EDGE_TYPE_REQUEST, from, to, req_type);
}

void get_resource_space_id(gpi_cap_t resource_type, uint64_t res_space_id, char *str_id)
{
    make_node_id(str_id, "SPACE", res_space_id);
}

void get_resource_id(gpi_cap_t res_type, uint64_t res_space_id, uint64_t res_id, char *str_id)
{
    make_node_id_2(str_id, cap_type_to_str(res_type), res_space_id, res_id);
}

void get_pd_id(uint64_t pd_id, char *str_id)
{
    make_node_id(str_id, "PD", pd_id);
}

gpi_model_node_t *add_resource_node(model_state_t *model_state, gpi_cap_t res_type, uint64_t res_space_id, uint64_t res_id)
{
    char node_id[CSV_MAX_STRING_SIZE];
    get_resource_id(res_type, res_space_id, res_id, node_id);

    return add_node(model_state, GPI_NODE_TYPE_RESOURCE, node_id, cap_type_to_str(res_type));
}

gpi_model_node_t *add_resource_space_node(model_state_t *model_state, gpi_cap_t resource_type, uint64_t res_space_id)
{
    char node_id[CSV_MAX_STRING_SIZE];
    get_resource_space_id(resource_type, res_space_id, node_id);

    return add_node(model_state, GPI_NODE_TYPE_SPACE, node_id, cap_type_to_str(resource_type));
}

// Add a PD to the model state
gpi_model_node_t *add_pd_node(model_state_t *model_state, char *pd_name, uint64_t pd_id)
{
    char node_id[CSV_MAX_STRING_SIZE];
    get_pd_id(pd_id, node_id);

    return add_node(model_state, GPI_NODE_TYPE_PD, node_id, pd_name);
}

gpi_model_node_t *get_root_node(model_state_t *model_state)
{
    return add_pd_node(model_state, "ROOT_TASK", 0);
}

// Add any nodes and edges from source state to dest state
void combine_model_states(model_state_t *dest, model_state_t *src)
{
    if (src->mem_start != NULL)
    {
        // Combine a portable source model state with the destination model state
        for (gpi_model_state_component_t *item = src->mem_start; item < src->mem_ptr; item += 1)
        {
            if (item->type == GPI_MODEL_NODE)
            {
                add_node(dest, item->node.node_type, item->node.id, item->node.data);
            }
            else if (item->type == GPI_MODEL_EDGE)
            {
                add_edge_by_id(dest, item->edge.k.type, item->edge.k.from, item->edge.k.to, item->edge.k.req_type);
            }
        }
    }
    else
    {
        // Combine a source model state from the same heap as the destination model state
        for (gpi_model_node_t *node = src->nodes; node != NULL; node = node->hh.next)
        {
            add_node(dest, node->node_type, node->id, node->data);
        }

        for (gpi_model_edge_t *edge = src->edges; edge != NULL; edge = edge->hh.next)
        {
            add_edge_by_id(dest, edge->k.type, edge->k.from, edge->k.to, edge->k.req_type);
        }
    }
}