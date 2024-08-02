#include "test_shared.h"

void extract_model(pd_client_context_t *pd_conn)
{
#ifdef GPI_EXTRACT_MODEL
    /* Print model state */
    int error = pd_client_dump(pd_conn, NULL, 0);
    assert(error == 0);
#endif
}
