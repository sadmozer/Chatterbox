/*
 * membox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */

/**
 * @file scan.h
 * @author Niccolò Cardelli 534015
 * @copyright 2018 Niccolò Cardelli
 * @brief Contiene le dichiarazioni delle funzioni di utilita' definite in scan.c
 */
#include <stdarg.h>

#include "list.h"
#include "icl_hash.h"
#include "message.h"

#if defined(c_plusplus) || defined(__cplusplus)
extern "C" {
#endif

#if !defined (SCAN_H_)
#define SCAN_H_

g_list * to_list(icl_hash_t *tab, pthread_mutex_t *mux);
int to_list_vfun(icl_entry_t *corr, void **argv);
int * post_all(icl_hash_t *ht, pthread_mutex_t *mux, message_t *message, int max_history_msgs);
int post_all_vfun(icl_entry_t *corr, void **argv);
char * set_offline(icl_hash_t *ht, pthread_mutex_t *mux, int fd);
int set_offline_vfun(icl_entry_t *corr, void **argv);
int read_operation(icl_hash_t *ht, pthread_mutex_t *mux, int fd, message_t *inmsg, message_t *inmsg_file, int *fdFound);
int read_operation_vfun(icl_entry_t *corr, void **argv);
#endif
