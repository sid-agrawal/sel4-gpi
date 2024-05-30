/**
 * @file linked_list.h
 * @author Linh Pham (phamhlinh01@gmail.com)
 * @brief Extremely simple, general usage linked list implementation
 * @version 0.1
 * @date 2024-05-27
 *
 * @copyright Copyright (c) 2024
 *
 */
#pragma once

typedef struct _linked_list_node
{
    void *data;
    struct _linked_list_node *next;
    struct _linked_list_node *prev;
} linked_list_node_t;

typedef struct _linked_list
{
    linked_list_node_t *head;
    linked_list_node_t *tail;
    size_t count;
} linked_list_t;

/**
 * @brief creates a new linked list on the heap, caller is responsible for freeing via linked_list_destroy
 *
 * @return linked_list_t* returns an empty linked list
 */
linked_list_t *linked_list_new(void);

/**
 * @brief inserts a new item containing the given data at the tail of the list
 * caller is responsible for managing given data's memory and for freeing the list node via linked_list_destroy
 *
 * @param list an existing list
 * @param data the data to insert
 */
void linked_list_insert(linked_list_t *list, void *data);

/**
 * @brief destroys all nodes in the list and the list itself
 * the data held by each node is NOT destroyed
 *
 * @param list an existing list
 */
void linked_list_destroy(linked_list_t *list);

/**
 * @brief inserts many items into the list at once
 *
 * @param list an existing list
 * @param count the number of items being inserted
 * @param ... variadic arguments specifying the items to insert
 */
void linked_list_insert_many(linked_list_t *list, int count, ...);
