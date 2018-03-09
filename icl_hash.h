/**
 * @file
 *
 * Header file for icl_hash routines.
 *
 */
/* $Id: icl_hash.h 1532 2010-09-07 15:38:18Z yarkhan $ */
/* $UTK_Copyright: $ */

#include "list.h"
#include <stdio.h>
#include <stdarg.h>
#ifndef icl_hash_h
#define icl_hash_h

#if defined(c_plusplus) || defined(__cplusplus)
extern "C" {
#endif


typedef struct icl_hash_s {
    int nbuckets;
    int nentries;
    icl_entry_t **buckets;
    unsigned int (*hash_function)(void*);
    int (*hash_key_compare)(void*, void*);
} icl_hash_t;

icl_hash_t *
icl_hash_create( int nbuckets, unsigned int (*hash_function)(void*), int (*hash_key_compare)(void*, void*) );

void
* icl_hash_find(icl_hash_t *, void* );

icl_entry_t
* icl_hash_insert(icl_hash_t *, void*, void *),
    * icl_hash_update_insert(icl_hash_t *, void*, void *, void **);

int
icl_hash_destroy(icl_hash_t *, void (*)(void*), void (*)(void*)),
    icl_hash_dump(FILE *, icl_hash_t *);

int icl_hash_delete( icl_hash_t *ht, void* key, void (*free_key)(void*), void (*free_data)(void*) );


#if defined(c_plusplus) || defined(__cplusplus)
}
#endif
void * icl_hash_apply_anyway(icl_hash_t * tab, pthread_mutex_t * mux, int vfun(icl_entry_t * corr, void * * args), void ** argv);
unsigned int hash_lock(unsigned int hash);
char * icl_hash_setoffline(icl_hash_t * ht, pthread_mutex_t * mux, int fd);
g_list * icl_hash_tolist(icl_hash_t * list, pthread_mutex_t * mux);
void icl_postall(icl_hash_t * ht, pthread_mutex_t * mux, char * sender, msg_data_t * message);
#endif /* icl_hash_h */

