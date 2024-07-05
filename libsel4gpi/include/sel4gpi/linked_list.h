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
 * @brief pops and returns an element from the head of the list
 * Caller is responsible for freeing the list node
 *
 * @param list an existing list
 * @param data returns the element from the head of the list
 *             if the list is empty, set to NULL
 */
void linked_list_pop_head(linked_list_t *list, void **data);

/**
 * @brief retrieves the data of an element at the given index in the list.
 *
 * @param list an existing list
 * @param idx the index of the item, returns NULL if idx is out of bounds
 * @return returns the element at the specified index, NULL if it doesn't exist
 */
void *linked_list_get_at_idx(linked_list_t *list, size_t idx);

/**
 * @brief destroys all nodes in the list and the list itself
 *
 * @param list an existing list
 * @param free_data if true, also frees the data in each node
 */
void linked_list_destroy(linked_list_t *list, bool free_data);

/**
 * @brief inserts many items into the list at once
 *
 * @param list an existing list
 * @param count the number of items being inserted
 * @param ... variadic arguments specifying the items to insert
 */
void linked_list_insert_many(linked_list_t *list, int count, ...);
