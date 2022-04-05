/**
 * @file counter_obj.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief 
 * @version 0.1
 * @date 2022-04-05
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include <libsel4gpi/counter_obj.h>

int counter_increment(counter_t *counter) {
    counter->value++;
    return 0;
}

int counter_decrement(counter_t *counter){
    counter->value--;
    return 0;
}