/*
 * membox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */

/**
 * @file group.c
 * @author Niccolò Cardelli 534015
 * @copyright 2018 Niccolò Cardelli 534015
 * @brief Contiene le funzioni di utilita' dei gruppi
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>

#include "list.h"
#include "message.h"
#include "connections.h"
#include "group.h"
#include "stats.h"
#include "config.h"

/**
 * @brief Libera la memoria allocata per un gruppo
 * @param  arg -- puntatore al gruppo
 */
void free_data_group(void *arg)
{
  icl_hash_destroy((icl_hash_t *) arg, my_free, NULL);
}

/**
 * @brief Crea un nuovo gruppo
 *
 * @return puntatore al nuovo gruppo
 */
icl_hash_t * new_group_data()
{
  return icl_hash_create(HASH_TABLE_GROUPS, NULL, string_compare);
}

/**
 * @brief Spedisce un messaggio a tutti i membri di un gruppo
 *
 * @param ht -- tabella hash
 * @param mux -- mutex dei bucket della tabella
 * @param message -- messaggio da inviare
 * @param members -- lista dei membri del gruppo
 * @param max_history_msgs -- numero massimo di messaggi nella history
 *
 * @return array contenente il numero di messaggi inviati e di messaggi archiviati
 * @details Wrapper di icl_hash_apply_until
 */
int * post_group(icl_hash_t *ht, pthread_mutex_t *mux, message_t *message, icl_hash_t *members, int max_history_msgs)
{
  int *info = (int*) malloc(sizeof(int) * 2);
  info[0] = 0;
  info[1] = 0;

  void *argv[5];
  argv[0] = (void*) message;
  argv[1] = (void*) members;
  argv[2] = (void*) &info[0];
  argv[3] = (void*) &info[1];
  argv[4] = (void*) &max_history_msgs;

  icl_hash_apply_until(ht, mux, post_group_vfun, argv);

  return info;
}

/**
 * @brief Spedisce un messaggio all'utente corrente se e solo se appartiene al gruppo
 *
 * @param corr -- nodo utente corrente
 * @param argv -- puntatore agli argomenti di input-output
 *                (messaggio da inviare,
 *                lista dei membri del gruppo,
 *                numero di messaggi inviati a utenti OFFLINE,
 *                numero di messaggi inviati a utenti ONLINE,
 *                numero massimo di messaggi nella history)
 * @return 0
 */
int post_group_vfun(icl_entry_t *corr, void **argv)
{

  user_data_t *curr_user_data = ((user_data_t *) corr->data);
  char *curr_user_name = (char*) corr->key;

  message_t *original_message = (message_t *) argv[0];
  icl_hash_t *members = (icl_hash_t *) argv[1];
  int *n_message_history = (int *) argv[2];
  int *n_message_sent = (int *) argv[3];
  int max_history_msgs = *((int *) argv[4]);

  if(icl_hash_find(members, curr_user_name) == NULL)
    return 0;

  msg_data_t *copy_hist_message;

  if (curr_user_data->fd == -1)
  {
    copy_hist_message = new_msg_data(original_message->hdr.op, original_message->data.buf, original_message->data.hdr.len);
    printf("%s manda il messaggio |%s| a %s: OFFLINE\n", original_message->hdr.sender, copy_hist_message->buf, curr_user_name);
    add_g(curr_user_data->pvmsg, new_string(original_message->hdr.sender), copy_hist_message);

    // ---- Controllo se il numero di messaggi nella history eccede la dimensione massima ----
    // ---- e in caso affermativo tolgo il meno recente ----
    if(curr_user_data->pvmsg->num > max_history_msgs) {
      icl_entry_t *tmp = remove2_g(curr_user_data->pvmsg, 0);
      my_free(tmp->key);
      free_data_msg(tmp->data);
      my_free(tmp);
    }
    (*n_message_history)++;
  }
  else
  {
    SYS(sendRequest(curr_user_data->fd, original_message), -1, "Error icl_hash_postall");
    printf("%s manda il messaggio |%s| a %s: ONLINE %d\n", original_message->hdr.sender, original_message->data.buf, curr_user_name, curr_user_data->fd);
    if(errno == EPIPE || errno == ECONNRESET) {
      copy_hist_message = new_msg_data(original_message->hdr.op, original_message->data.buf, original_message->data.hdr.len);
      printf("%s manda il messaggio |%s| a %s: OFFLINE\n", original_message->hdr.sender, copy_hist_message->buf, curr_user_name);

      add_g(curr_user_data->pvmsg, new_string(original_message->hdr.sender), copy_hist_message);

      // ---- Controllo se il numero di messaggi nella history eccede la dimensione massima ----
      // ---- e in caso affermativo tolgo il meno recente ----
      if(curr_user_data->pvmsg->num > max_history_msgs){
        icl_entry_t *tmp = remove2_g(curr_user_data->pvmsg, 0);
        my_free(tmp->key);
        free_data_msg(tmp->data);
        my_free(tmp);
      }
      (*n_message_history)++;
    }
    else {
      (*n_message_sent)++;
    }
  }
  return 0;
}
