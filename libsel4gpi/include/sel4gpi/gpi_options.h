#pragma once

/**
 * @file
 * Compile-time options for the GPI server
*/

/**
 * Define the cleanup policy for crashed resource manager PDs.
 * See pd_cleanup_policy_t for options.
*/
#define GPI_CLEANUP_POLICY PD_CLEANUP_DEPENDENTS_RECURSIVE

/**
 * If true, forge the test process as a PD.
*/
#define PD_FORGE 1

/**
 * If true:     Resource servers call the root task to notify it when map relations are created.
 * If false:    Resource servers only expose map relations when dumping resource relations.
*/
#define TRACK_MAP_RELATIONS 0

/**
 * If true:     Resource servers will store reply caps as soon as they receive a message.
 *              If the resource server crashes while serving a request, the root task will respond to a blocked client 
 *              using the reply cap.
 * 
 * If false:    Resource servers will not store reply caps.
 *              If the resource server crashes while serving a request, the client will remain blocked.
*/
#define STORE_REPLY_CAP 1