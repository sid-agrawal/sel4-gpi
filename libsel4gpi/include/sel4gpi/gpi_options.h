#pragma once

/**
 * @file
 * Compile-time options for the GPI server
*/

/**
 * Define the cleanup depth for crashed resource manager PDs.
 * For each resource space that the killed PD was managing, the cleanup policy will be invoked.
 * 
 * Resource space depth:
 * - Delete dependent resource spaces up to depth N from the crashed resource manager's resource spaces
 * 
 * PD depth:
 * - Delete dependent PDs up to depth N from the crashed resource manager
 * 
 * A depth of -1 means we will traverse infinitely
 * 
 * Sample policies:
 * 
 * CLEANUP_RESOURCE_SPACES_DIRECT: resource_space_depth 1, pd_depth 0
 *  Any resources from the deleted resource space will be removed from other PDs that hold them.
 *  Any PDs with RDEs for the deleted resource space will have the RDE removed.
 * 
 * CLEANUP_RESOURCE_SPACES_RECURSIVE: resource_space_depth -1, pd_depth 0
 *  (XXX) Arya: Not implemented
 *  Performs the same steps as PD_CLEANUP_MINIMAL, except it also recursively deletes any resources that mapped 
 *  to other deleted resources.
 * 
 * CLEANUP_DEPENDENTS_DIRECT: resource_space_depth 0, pd_depth 1
 *  Kill any PDs that either had an RDE for the deleted resource space, 
 *  or held resources from the deleted resource space.
 * 
 * CLEANUP_DEPENDENTS_RECURSIVE: resource_space_depth 0, pd_depth -1
 *  Same as CLEANUP_DEPENDENTS_DIRECT, except it also recursively kills any PDs that depended on other killed PDs.
*/
#define GPI_CLEANUP_RESOURCE_SPACE_DEPTH 1
#define GPI_CLEANUP_PD_DEPTH 0

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