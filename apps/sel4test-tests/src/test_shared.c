#include "test_shared.h"

void extract_model(pd_client_context_t *pd_conn)
{
#ifdef GPI_EXTRACT_MODEL
    /* Print model state */
    int error = pd_client_dump(pd_conn, NULL, 0);
    assert(error == 0);
#endif
}

int maybe_terminate_pd(pd_client_context_t *pd_conn)
{
    int error = pd_client_terminate(pd_conn);

    if (error == seL4_InvalidCapability || error == seL4_InvalidArgument)
    {
        printf("WARNING: Failed to cleanup PD (%u) due to invalid cap, "
               "this may be expected if the cleanup policy already destroyed it. \n",
               pd_conn->id);

        error = seL4_NoError;
    }
    else if (error != seL4_NoError)
    {
        printf("ERROR: Failed to cleanup PD (%u), error %d is not expected. \n",
               pd_conn->id, error);
    }

    return error;
}