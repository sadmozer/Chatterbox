/*
 * membox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */

 /**
 * @file group.h
 * @author Niccolò Cardelli 534015
 * @copyright 2018 Niccolò Cardelli 534015
 * @brief Contiene le dichiarazioni delle funzioni di utilita' dei gruppi definite in group.c
 */

#include <pthread.h>
#include "list.h"
#include "config.h"
#include "message.h"

#if !defined (GROUP_H_)
#define GROUP_H_


void free_data_group(void *arg);
icl_hash_t * new_group_data();

int * post_group(icl_hash_t *ht, pthread_mutex_t *mux, message_t *message, icl_hash_t *members, int max_history_msgs);
int post_group_vfun(icl_entry_t *corr, void **argv);

#endif
