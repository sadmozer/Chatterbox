#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "icl_hash.h"
#include "message.h"
#include "connections.h"

group_data_t * new_group_data(int nbuckets, int numlock){
    group_data_t * nu_grp_data = (group_data_t *) my_malloc(sizeof(group_data_t));
    nu_grp_data->joiners = icl_hash_create(nbuckets, NULL, NULL);
    pthread_mutex_init(&nu_grp_data->mux_joiners, NULL);
    nu_grp_data->lock_mask = (int*) my_malloc(sizeof(int)*numlock);
    return nu_grp_data;
}
void icl_hash_postgrp(icl_hash_t * ht, pthread_mutex_t * mux, char * sender, group_data_t * grp, msg_data_t * message){
    int i;
    unsigned int curr_lock_index;
    icl_entry_t *corr;
    message_t msg;
    setHeader(&msg.hdr, message->type, sender);
    setData(&msg.data, "", message->buf, message->len);
    PTHREAD_MUTEX_LOCK(&(grp->mux_joiners), "icl_hash_postgrp mux");
    for (i = 0; i < ht->nbuckets; i++)
    {
        curr_lock_index= hash_lock(i);
        if (grp->lock_mask[curr_lock_index] == 1)
        {
            if (curr_lock_index != hash_lock(i - 1))
                PTHREAD_MUTEX_LOCK(&mux[curr_lock_index], "icl_hash_postall mux");
            corr = ht->buckets[i];
            while (corr != NULL)
            {
                if (icl_hash_find(grp->joiners, (void *)corr->key) != NULL)
                {
                    if (ht->hash_key_compare((char *)corr->key, sender) == 0 
                    && ((user_data_t *)corr->data)->status == 0)
                    {
                        add_g(((user_data_t *)corr->data)->pvmsg, sender, message);
                    }
                    else
                    {
                        SYS(sendRequest(((user_data_t *)corr->data)->fd, &msg), -1, "Error icl_hash_postall");
                    }
                }
                corr = corr->next;
            }
            if (curr_lock_index != hash_lock(i - 1))
                PTHREAD_MUTEX_UNLOCK(&mux[curr_lock_index], "icl_hash_postall mux");
        }
    }
    PTHREAD_MUTEX_UNLOCK(grp->mux_joiners, "icl_hash_postgrp mux");    
}