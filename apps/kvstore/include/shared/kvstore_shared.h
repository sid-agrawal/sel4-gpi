#pragma once

#include <sel4/sel4.h>
#include <sel4/types.h>

#define KVSTORE_DEBUG 1

/* API of the kvstore server */

/* IPC values returned in the "label" message header. */
enum kvstore_errors
{
    KVSTORE_NOERROR = 0,
    /* No future collisions with seL4_Error.*/
    KVSTORE_ERROR_KEY = seL4_NumErrors,
    KVSTORE_ERROR_UNKNOWN,
};

/* IPC Message register values for RDMSGREG_FUNC */
enum kvstore_server_funcs
{
    KV_FUNC_SET_REQ = 0,
    KV_FUNC_SET_ACK,

    KV_FUNC_GET_REQ,
    KV_FUNC_GET_ACK,
};

enum kvstore_msgregs
{
    /* These are fixed headers in every kvstore message. */
    KVMSGREG_FUNC = 0,

    /* This is a convenience label for IPC MessageInfo length. */
    KVMSGREG_LABEL0,

    /* Set */
    KVMSGREG_SET_REQ_KEY = KVMSGREG_LABEL0,
    KVMSGREG_SET_REQ_VAL,
    KVMSGREG_SET_REQ_END,
    KVMSGREG_SET_ACK_END = KVMSGREG_LABEL0,

    /* Get */
    KVMSGREG_GET_REQ_KEY = KVMSGREG_LABEL0,
    KVMSGREG_GET_REQ_END,
    KVMSGREG_GET_ACK_VAL = KVMSGREG_LABEL0,
    KVMSGREG_GET_ACK_END,
};