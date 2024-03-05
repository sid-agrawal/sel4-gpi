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

static void insert_row(model_state_t *model_state, csv_row_t *new_row)
{

    assert(model_state != NULL);
    assert(new_row != NULL);

    // Add node to the front of the list after the heading row
    csv_row_t *header_row = model_state->csv_rows;
    csv_row_t *old_first_row = header_row->next;

    header_row->next = new_row;
    model_state->csv_rows->next = new_row;
    new_row->next = old_first_row;
    model_state->csv_rows_len++;
}

static void init_row(csv_row_t *row)
{
    snprintf(row->resource_from, CSV_MAX_STRING_SIZE, "%s", "");
    snprintf(row->resource_to, CSV_MAX_STRING_SIZE, "%s", "");
    snprintf(row->resource_type, CSV_MAX_STRING_SIZE, "%s", "");
    snprintf(row->resource_id, CSV_MAX_STRING_SIZE, "%s", "");
    snprintf(row->pd_name, CSV_MAX_STRING_SIZE, "%s", "");
    snprintf(row->pd_from, CSV_MAX_STRING_SIZE, "%s", "");
    snprintf(row->pd_to, CSV_MAX_STRING_SIZE, "%s", "");
    snprintf(row->pd_id, CSV_MAX_STRING_SIZE, "%s", "");
    snprintf(row->is_mapped, CSV_MAX_STRING_SIZE, "%s", "");
}

static void init_rr_row(csv_rr_row_t *row)
{
    snprintf(row->resource_from, CSV_MAX_STRING_SIZE, "%s", "");
    snprintf(row->resource_to, CSV_MAX_STRING_SIZE, "%s", "");
    snprintf(row->resource_type, CSV_MAX_STRING_SIZE, "%s", "");
    snprintf(row->resource_id, CSV_MAX_STRING_SIZE, "%s", "");
}

// Copy a resource relation row into a full model state row
static void copy_row(csv_row_t *to, csv_rr_row_t *from)
{
    init_row(to);
    memcpy(to, from, sizeof(csv_rr_row_t));
}

void init_model_state(model_state_t *model_state)
{

    csv_row_t *new_row = (csv_row_t *)malloc(sizeof(csv_row_t));
    assert(new_row != NULL);
    snprintf(new_row->resource_from, CSV_MAX_STRING_SIZE, "%s", "RESOURCE_FROM");
    snprintf(new_row->resource_to, CSV_MAX_STRING_SIZE, "%s", "RESOURCE_TO");
    snprintf(new_row->resource_type, CSV_MAX_STRING_SIZE, "%s", "RES_TYPE");
    snprintf(new_row->resource_id, CSV_MAX_STRING_SIZE, "%s", "RES_ID");
    snprintf(new_row->pd_name, CSV_MAX_STRING_SIZE, "%s", "PD_NAME");
    snprintf(new_row->pd_from, CSV_MAX_STRING_SIZE, "%s", "PD_FROM");
    snprintf(new_row->pd_to, CSV_MAX_STRING_SIZE, "%s", "PD_TO");
    snprintf(new_row->pd_id, CSV_MAX_STRING_SIZE, "%s", "PD_ID");
    snprintf(new_row->is_mapped, CSV_MAX_STRING_SIZE, "%s", "IS_MAPPED");

    new_row->next = NULL;
    model_state->csv_rows = new_row;

    model_state->csv_rows_len = 0;
    model_state->num_resources = 0;
    model_state->num_pds = 0;

    model_state->num_depends_on = 0;
    model_state->num_has_access_to = 0;
    model_state->num_requests = 0;
}

void init_rr_state(rr_state_t *model_state)
{
    model_state->csv_rows = NULL;
    model_state->csv_rows_len = 0;
    model_state->num_resources = 0;
    model_state->num_depends_on = 0;
}

// Function to export the model state to a buffer with CSV formatting
void export_model_state(model_state_t *model_state, char *buffer, size_t buf_len)
{

    assert(model_state != NULL);
    assert(model_state->csv_rows != NULL);
    assert(model_state->csv_rows_len != 0);

    size_t buf_written_total = 0;
    csv_row_t *current_row = model_state->csv_rows;
    uint8_t width = 0;

    while (current_row != NULL)
    {
        size_t buf_written = snprintf(buffer, buf_len - buf_written_total,
                                      "%-*s,%-*s,%-*s,%-*s,%-*s,%-*s,%-*s,%-*s,%-*s\n",
                                      width,
                                      current_row->resource_from,
                                      width,
                                      current_row->resource_to,
                                      width,
                                      current_row->resource_type,
                                      width,
                                      current_row->resource_id,
                                      width,
                                      current_row->pd_name,
                                      width,
                                      current_row->pd_from,
                                      width,
                                      current_row->pd_to,
                                      width,
                                      current_row->pd_id,
                                      width,
                                      current_row->is_mapped);

        buffer += buf_written;
        buf_written_total += buf_written;

        current_row = current_row->next;

        if (buf_written_total >= buf_len)
        {
            ZF_LOGF("Buffer overflow");
        }
    }
}

// Function to export the model state to a buffer with CSV formatting
void print_model_state(model_state_t *model_state)
{

    assert(model_state != NULL);
    assert(model_state->csv_rows != NULL);
    assert(model_state->csv_rows_len != 0);

    size_t buf_written_total = 0;
    csv_row_t *current_row = model_state->csv_rows;
    uint8_t width = 0;

    while (current_row != NULL)
    {
        printf("%-*s,%-*s,%-*s,%-*s,%-*s,%-*s,%-*s,%-*s,%-*s\n",
               width,
               current_row->resource_from,
               width,
               current_row->resource_to,
               width,
               current_row->resource_type,
               width,
               current_row->resource_id,
               width,
               current_row->pd_name,
               width,
               current_row->pd_from,
               width,
               current_row->pd_to,
               width,
               current_row->pd_id,
               width,
               current_row->is_mapped);

        current_row = current_row->next;
    }
}

// Add any resource relations from a rr_state_t to the model state
void combine_model_states(model_state_t *ms, rr_state_t *rs)
{
    for (int i = 0; i < rs->csv_rows_len; i++)
    {
        csv_rr_row_t *from_row = &rs->csv_rows[i];
        csv_row_t *new_row = (csv_row_t *)malloc(sizeof(csv_row_t));
        copy_row(new_row, from_row);
        insert_row(ms, new_row);
    }

    ms->csv_rows_len += rs->csv_rows_len;
    ms->num_depends_on += rs->num_depends_on;
    ms->num_resources += rs->num_resources;
}

// Function to add a resource to the model state
void add_resource(model_state_t *model_state, char *resource_type, char *resource_id)
{
    assert(strlen(resource_type) != 0 && strlen(resource_id) != 0);
    assert(strlen(resource_type) < CSV_MAX_STRING_SIZE && strlen(resource_id) < CSV_MAX_STRING_SIZE);

    csv_row_t *new_row = (csv_row_t *)malloc(sizeof(csv_row_t));
    assert(new_row != NULL);
    init_row(new_row);

    // Set the resource type and ID
    snprintf(new_row->resource_type, CSV_MAX_STRING_SIZE, "%s", resource_type);
    snprintf(new_row->resource_id, CSV_MAX_STRING_SIZE, "%s", resource_id);

    // Add node to the front of the list after the heading row
    insert_row(model_state, new_row);
    model_state->num_resources++;
}

// Function to add a PD to the model state
void add_pd(model_state_t *model_state, char *pd_name, char *pd_id)
{

    assert(strlen(pd_name) != 0 && strlen(pd_id) != 0);
    assert(strlen(pd_name) < CSV_MAX_STRING_SIZE && strlen(pd_id) < CSV_MAX_STRING_SIZE);

    csv_row_t *new_row = (csv_row_t *)malloc(sizeof(csv_row_t));
    assert(new_row != NULL);
    init_row(new_row);

    // Set the resource type and ID
    snprintf(new_row->pd_name, CSV_MAX_STRING_SIZE, "%s", pd_name);
    snprintf(new_row->pd_id, CSV_MAX_STRING_SIZE, "%s", pd_id);

    // Add node to the front of the list after the heading row
    insert_row(model_state, new_row);
    model_state->num_pds++;
}

// Function to add a mapping between a PD and a resource to the model state
void add_has_access_to(model_state_t *model_state,
                       char *pd_from,
                       char *resource_to,
                       bool is_mapped)
{
    assert(strlen(pd_from) != 0 && strlen(resource_to) != 0);
    assert(strlen(pd_from) < CSV_MAX_STRING_SIZE && strlen(resource_to) < CSV_MAX_STRING_SIZE);

    csv_row_t *new_row = (csv_row_t *)malloc(sizeof(csv_row_t));
    assert(new_row != NULL);
    init_row(new_row);

    // Set the resource type and ID
    snprintf(new_row->pd_from, CSV_MAX_STRING_SIZE, "%s", pd_from);
    snprintf(new_row->resource_to, CSV_MAX_STRING_SIZE, "%s", resource_to);
    snprintf(new_row->is_mapped, CSV_MAX_STRING_SIZE, "%s", is_mapped ? "TRUE" : "FALSE");

    // Add node to the front of the list after the heading row
    insert_row(model_state, new_row);
    model_state->num_has_access_to++;
}

// Function to add a resource relationship to the model state
void add_resource_depends_on(model_state_t *model_state, char *resource_from, char *resource_to)
{
    assert(strlen(resource_from) != 0 && strlen(resource_to) != 0);
    assert(strlen(resource_from) < CSV_MAX_STRING_SIZE && strlen(resource_to) < CSV_MAX_STRING_SIZE);

    csv_row_t *new_row = (csv_row_t *)malloc(sizeof(csv_row_t));
    assert(new_row != NULL);
    init_row(new_row);

    // Set the resource type and ID
    snprintf(new_row->resource_from, CSV_MAX_STRING_SIZE, "%s", resource_from);
    snprintf(new_row->resource_to, CSV_MAX_STRING_SIZE, "%s", resource_to);

    // Add node to the front of the list after the heading row
    insert_row(model_state, new_row);
    model_state->num_depends_on++;
}

// Function to add a resource to the rr state
void add_resource_rr(rr_state_t *model_state, char *resource_type, char *resource_id, csv_rr_row_t *new_row)
{
    assert(strlen(resource_type) != 0 && strlen(resource_id) != 0);
    assert(strlen(resource_type) < CSV_MAX_STRING_SIZE && strlen(resource_id) < CSV_MAX_STRING_SIZE);

    assert(new_row != NULL);
    init_rr_row(new_row);

    // Set the resource type and ID
    snprintf(new_row->resource_type, CSV_MAX_STRING_SIZE, "%s", resource_type);
    snprintf(new_row->resource_id, CSV_MAX_STRING_SIZE, "%s", resource_id);
    model_state->num_resources++;

    // Set list pointer if this is the first row
    if (model_state->csv_rows == NULL)
    {
        model_state->csv_rows = new_row;
    }
    model_state->csv_rows_len++;
}

// Function to add a resource relationship to the rr state
void add_resource_depends_on_rr(rr_state_t *model_state, char *resource_from, char *resource_to, csv_rr_row_t *new_row)
{
    assert(strlen(resource_from) != 0 && strlen(resource_to) != 0);
    assert(strlen(resource_from) < CSV_MAX_STRING_SIZE && strlen(resource_to) < CSV_MAX_STRING_SIZE);

    assert(new_row != NULL);
    init_rr_row(new_row);

    // Set the resource type and ID
    snprintf(new_row->resource_from, CSV_MAX_STRING_SIZE, "%s", resource_from);
    snprintf(new_row->resource_to, CSV_MAX_STRING_SIZE, "%s", resource_to);
    model_state->num_depends_on++;

    // Set list pointer if this is the first row
    if (model_state->csv_rows == NULL)
    {
        model_state->csv_rows = new_row;
    }
    model_state->csv_rows_len++;
}

// Function add a PD to PD relationship to the model state
void add_pd_requestes(model_state_t *model_state, char *pd_from, char *pd_to)
{

    assert(strlen(pd_from) != 0 && strlen(pd_to) != 0);
    assert(strlen(pd_from) < CSV_MAX_STRING_SIZE && strlen(pd_to) < CSV_MAX_STRING_SIZE);

    csv_row_t *new_row = (csv_row_t *)malloc(sizeof(csv_row_t));
    assert(new_row != NULL);
    init_row(new_row);

    // Set the resource type and ID
    snprintf(new_row->pd_from, CSV_MAX_STRING_SIZE, "%s", pd_from);
    snprintf(new_row->pd_to, CSV_MAX_STRING_SIZE, "%s", pd_to);

    // Add node to the front of the list after the heading row
    insert_row(model_state, new_row);
    model_state->num_requests++;
}
