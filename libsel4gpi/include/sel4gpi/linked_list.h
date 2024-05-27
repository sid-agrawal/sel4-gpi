#pragma once

/* general usage linked list */
typedef struct _linked_list_node
{
    void *data;
    struct _linked_list *next;
    struct _linked_list *prev;
} linked_list_node_t;

typedef struct _linked_list
{
    linked_list_node_t *head;
    linked_list_node_t *tail;
    size_t count;
} linked_list_t;

linked_list_t *linked_list_new(void);

void linked_list_insert(linked_list_t *list, void *data);