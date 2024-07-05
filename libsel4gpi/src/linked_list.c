#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sel4gpi/linked_list.h>

void linked_list_insert_many(linked_list_t *list, int count, ...)
{
    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; i++)
    {
        void *varg = va_arg(args, void *);
        linked_list_insert(list, varg);
    }
}

void linked_list_insert(linked_list_t *list, void *data)
{
    if (list)
    {
        linked_list_node_t *new_node = calloc(1, sizeof(linked_list_node_t));
        new_node->data = data;
        new_node->next = NULL;

        if (!list->head)
        {
            list->head = new_node;
            new_node->prev = NULL;
        }

        if (list->tail)
        {
            list->tail->next = new_node;
            new_node->prev = list->tail;
        }

        list->tail = new_node;
        list->count++;
    }
}

void linked_list_pop_head(linked_list_t *list, void **data)
{
    if (list && list->head)
    {
        linked_list_node_t *node = list->head;
        *data = node->data;
        list->head = node->next;
        list->count--;

        if (list->head)
        {
            list->head->prev = NULL;
        }

        free(node);
    }
    else {
        *data = NULL;
    }
}

void *linked_list_get_at_idx(linked_list_t *list, size_t idx)
{
    void *ret = NULL;
    if (list && list->head && idx < list->count)
    {
        size_t curr_idx = 0;
        linked_list_node_t *curr = list->head;
        while (curr_idx < idx && curr != NULL)
        {
            curr_idx++;
            curr = curr->next;
        }

        if (curr)
        {
            ret = curr->data;
        }
    }

    return ret;
}

linked_list_t *linked_list_new(void)
{
    return calloc(1, sizeof(linked_list_t));
}

void linked_list_destroy(linked_list_t *list, bool free_data)
{
    if (list)
    {
        linked_list_node_t *curr = list->head;
        linked_list_node_t *next = NULL;
        while (curr != NULL)
        {
            next = curr->next;

            if (free_data)
            {
                free(curr->data);
            }

            free(curr);
            curr = next;
        }

        /* this may sometimes be a non-malloc'd list, which is fine*/
        free(list);
    }
}
