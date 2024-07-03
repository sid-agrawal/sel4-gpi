#include <sel4test/test.h>
#include <sel4test/macros.h>
#include <sel4gpi/pd_obj.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/pd_utils.h>

#include <sel4utils/thread.h>
#include <sel4gpi/debug.h>
#include "../test.h"
#include "../helpers.h"
#include <stdio.h>

#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/cpu_clientapi.h>
#include <sel4bench/arch/sel4bench.h>

bool lwc_server_in_local = true;

/*
    This is not exactly an LWC, but it is a good starting point.

    In LWC, the address spaces are isolated by default and sharing is explicit.
    In this case, we are sharing the address space by default at the time of creation of the LWC.

    But isolated there after. So, after the AS is created we share the page for the key with LWC
    and clear it in the parent.

    What kind of threat model does this give us?

    1. It means that the parent doesn't have access to the key and only the lib has access to the key.
    2. The parent can't access the key even if it tries to, as it is in a different AS.
    n



From the paper:
  * Algo 1: Snapshot and rollback, make a complete COW copy (AS and FDs)
  * Algo 2: Isolate Session, make a complete of AS, and share one FDs
  * Algo 3: Restrict Key to the libary, make a complte copy of AS and then overlay just the key, clear the key in the parent
  * Algo 4: SYScall Intercept:
*/

/*
    Below are functions that we used to create and then switch to an LWC.
    If this logic is implemented in the GPI server then it is atomic from the point of
    view of the client (i.e., this test).
*/

/*
   Let's build a concrete example of how we would use the LWC API.
   In this case, the parent LWC is trusted but the child is not.

   The parent LWC will create a child LWC and then switch to it.
   The parent LWC will have access to the Child's CSpace and VSpace.

*/

struct lwc_info
{
    /* replace these with OSMosis caps */
    seL4_CPtr cspace_root;
    seL4_CPtr vspace_root;
    void *func_to_run;
};

/* Either in lwccreate or swtich we need to update the env_t which has a bunch of info about the env*/
int lwcCreate(env_t env, char key[])
{

    if (lwc_server_in_local)
    {
        /* Create a new PD the AS */

        // pd_cap = new PD

        /* Create a copy of the AS and send this cap to the new PD */
        // AS to PD
        int error;
        ads_client_context_t conn = sel4gpi_get_bound_vmr_rde();

        // Using a known EP, get a new ads CAP.
        ads_client_context_t lwc_ads_conn;
        error = ads_client_shallow_copy(&conn, &env->vka, (void *)0,
                                        /* We cannot use this, as in this case when the parent clears the key, it will clear in the child too */
                                        &lwc_ads_conn);
        test_error_eq(error, 0);
    }
    else
    {

        /*
            Create an IPC message to make an LWC
            Get a badges cap in return using which you can communicate with the LWC Server (not the LWC)
        */
    }
}

int lwcSwitch()
{
    if (lwc_server_in_local)
    {
        /* Change AS */
    }
    else
    {

        /*
            Create an IPC message to make an LWC
            Get a badges cap in return using which you can communicate with the LWC Server (not the LWC)
        */
    }
}

char key[256];
int sign(char *data)
{

    if (lwc_server_in_local)
    {

        /* Make sure that the key is readable */
        // osm_cap_t ds = get_ds(data);
        // For now, let's just call it the PFrame

        printf("Signing data: %s \t   with key: %s\n", data, key);
        /* attach new DS to the LWC's AS*/

        /* Change AS */
        // 1. Get the AS cap
        // 2. Get the TCB Cap
        // int error = cpu_client_connect bind AS

        /* Call Function */

        /* Unmap DS */

        /* Change AS Back */
    }
}

int test_lwc(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    /* Load Key from file*/
    snprintf(key, 256, "%s", "Hello World");

    /* create LWC the LWC has access to the key*/
    // int lwc_id = lwcCreate(env, key); /* copy the entire AS, say which func to run*/

    ads_client_context_t conn = sel4gpi_get_bound_vmr_rde();
    cpu_client_context_t cpu_conn = sel4gpi_get_cpu_conn();

    printf("Change to new VSpace \n");
    // Using a known EP, get a new ads CAP.
    ads_client_context_t lwc_ads_conn;

    error = ads_client_shallow_copy(&conn, &env->vka, (void *)0,
                                    /* We cannot use this, as in this case when the parent clears the key, it will clear in the child too */
                                    &lwc_ads_conn);
    test_error_eq(error, 0);

    printf("%d: KEY: %s\n", __LINE__, key);

    key[0] = '\0';
    printf("%d: KEY: %s\n", __LINE__, key);

    error = cpu_client_change_vspace(&cpu_conn, &lwc_ads_conn);
    test_error_eq(error, 0);

    /* Delete cap to child's AS This is similar to lwcRestrict */
    printf("Start of signing with key: %s\n", key);
    printf("%d: KEY: %s\n", __LINE__, key);

    /* call sign */
    char *data = malloc(256);
    assert(data != NULL);
    data = "foobar";
    sign(data);

    printf("%d: KEY: %s\n", __LINE__, key);
    // while (1);
    printf("Change to old VSpace \n");
    error = cpu_client_change_vspace(&cpu_conn, &conn);
    test_error_eq(error, 0);

    printf("%d: KEY: %s\n", __LINE__, key);

    return 0;
}

DEFINE_TEST(GPILWC001, "OSMO: Create and swtich to LWC", test_lwc, true)