#include <stdlib.h>
#include <stdio.h>
#include <sel4gpi/linked_list.h>

static linked_list_node_t *insert_from_tail(linked_list_node_t *tail, void *data)
{
}

void linked_list_insert(linked_list_t *list, void *data)
{
    if (list)
    {
        linked_list_node_t *new_node = calloc(1, sizeof(linked_list_node_t));
        new_node->data = data;

        if (!list->head)
        {
            list->head = new_node;
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

linked_list_t *linked_list_new(void)
{
    return calloc(1, sizeof(linked_list_t));
}

void linked_list_destroy(linked_list_t *list)
{
    if (list)
    {
        linked_list_node_t *curr = list->head;
        linked_list_node_t *next = NULL;
        while (curr != NULL)
        {
            next = curr->next;
            free(curr);
            curr = next;
        }

        free(list);
    }
}
