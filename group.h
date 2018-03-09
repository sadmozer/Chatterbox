
#ifndef GROUP_H_
#define GROUP_H_

#include <pthread.h>
#include "icl_hash.h"
#include "config.h"

typedef struct group{
    pthread_mutex_t mux_joiners;
    icl_hash_t * joiners;
    int * lock_mask;
} group_data_t;

group_data_t * new_group_data(int nbuckets, int numlock);
void icl_hash_postgrp(icl_hash_t * ht, pthread_mutex_t * mux, char * sender, group_data_t * grp, msg_data_t * message);
#endif
