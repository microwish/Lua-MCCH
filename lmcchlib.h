#ifndef _LMCCHLIB_H
#define _LMCCHLIB_H
#pragma once

#include <lua.h>
#include <lauxlib.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern unsigned int mc_hash_crc32(const char *, int);

#define MC_CONSISTENT_POINTS 160/* points per server */
#define MC_CONSISTENT_BUCKETS 1024/* number of precomputed buckets, should be power of 2 */

typedef struct {
  //int sockfd;
	char *host;
	unsigned short port;
	int timeout_ms;
	//int status;
} mc_t;

typedef unsigned int (*mc_hash_function)(const char *, int);

typedef struct {
	mc_t *server;
	unsigned int point;
} mc_consistent_point_t;

typedef struct {
	int nservers;
	mc_consistent_point_t *points;
	int	npoints;
	mc_t *buckets[MC_CONSISTENT_BUCKETS];
	int buckets_populated;
	mc_hash_function hash;
} mc_consistent_state_t;

/* hashing strategy */
typedef void *(*mc_hash_create_state)(mc_hash_function);
typedef void (*mc_hash_free_state)(void *);
typedef mc_t *(*mc_hash_find_server)(void *, const char *, int);
typedef void (*mc_hash_add_server)(void *, mc_t *, unsigned int);

typedef struct {
	mc_hash_create_state create_state;
	mc_hash_free_state free_state;
	mc_hash_find_server find_server;
	mc_hash_add_server add_server;
} mc_hash_t;

typedef struct {
	mc_t **servers;
	int nservers;
	mc_hash_t *hash;
	void *hash_state;
} mc_pool_t;

extern void *mc_consistent_create_state(mc_hash_function);
extern void mc_consistent_add_server(void *, mc_t *, unsigned int);
extern mc_t *mc_consistent_find_server(void *, const char *, int);

extern mc_hash_t mc_consistent_hash;

#endif//_LMCCHLIB_H
