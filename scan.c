/*
 * membox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */

 /**
 * @file scan.c
 * @author Niccolò Cardelli 534015
 * @copyright 2018 Niccolò Cardelli
 * @brief Contiene funzioni di utilita' per le operazioni che richiedono
 *        una scansione della tabella hash
 */

#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include "message.h"
#include "icl_hash.h"
#include "scan.h"
#include "list.h"
#include "connections.h"
#include "config.h"
#include "stats.h"


/**
 * @brief Genera la lista degli utenti connessi
 *
 * @param tab -- tabella hash
 * @param mux -- mutex dei bucket della tabella
 *
 * @return lista di utenti connessi
 * @details Wrapper di icl_hash_apply_until
 */
g_list * to_list(icl_hash_t *tab, pthread_mutex_t *mux)
{
  g_list *nu_list = new_glist(tab->hash_key_compare, NULL, my_free);
  icl_hash_apply_until(tab, mux, to_list_vfun, (void**) &nu_list);
  return nu_list;
}

/**
 * @brief Aggiunge l'utente corrente alla lista degli utenti connessi
 *
 * @param corr -- nodo utente corrente
 * @param argv -- puntatore agli argomenti di input-output
 *                (nuova lista vuota)
 * @return 0
 */
int to_list_vfun(icl_entry_t * corr, void ** argv)
{
  user_data_t * curr_user_data = ((user_data_t *) corr->data);
  char * curr_user_name = (char*) corr->key;
  if(curr_user_data->fd != -1)
    add_g((g_list*)(*argv), new_string(curr_user_name), NULL);
  return 0;
}

/**
 * @brief Spedisce un messaggio a tutti gli utenti
 *
 * @param ht -- tabella hash
 * @param mux -- mutex dei bucket della tabella
 * @param message -- messaggio da inviare
 * @param max_history_msgs -- numero massimo di messaggi nella history
 *
 * @return array contenente il numero di messaggi inviati e di messaggi archiviati
 * @details Wrapper di icl_hash_apply_until
 */
int * post_all(icl_hash_t *ht, pthread_mutex_t *mux, message_t *message, int max_history_msgs)
{
  int *info = (int*) malloc(sizeof(int) * 2);
  info[0] = 0;
  info[1] = 0;

  void *argv[4];
  argv[0] = (void*) message;
  argv[1] = (void*) &info[0];
  argv[2] = (void*) &info[1];
  argv[3] = (void*) &max_history_msgs;

  icl_hash_apply_until(ht, mux, post_all_vfun, argv);
  return info;
}

/**
 * @brief Spedisce un messaggio all'utente corrente.
 *
 * Spedisce il messaggio all'utente se il suo fd è valido oppure lo
 * archivia in caso l'utente sia OFFLINE o in caso di broken pipe o errore.
 *
 * @param corr -- nodo utente corrente
 * @param argv -- puntatore agli argomenti di input-output
 *                (messaggio da inviare,
 *                numero di messaggi inviati a utenti OFFLINE,
 *                numero di messaggi inviati a utenti ONLINE,
 *                numero massimo di messaggi nella history)
 * @return 0
 */
int post_all_vfun(icl_entry_t *corr, void **argv)
{
  user_data_t *curr_user_data = ((user_data_t*) corr->data);
  char *curr_user_name = (char*) corr->key;

  message_t *original_message = (message_t*) argv[0];
  int *n_message_history = (int*) argv[1];
  int *n_message_sent = (int*) argv[2];
  int max_history_msgs = *((int*) argv[3]);

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
    printf("%s manda il messaggio |%s| a %s: ONLINE %d\n", original_message->hdr.sender, original_message->data.buf, curr_user_name, curr_user_data->fd);
    SYS(sendRequest(curr_user_data->fd, original_message), -1, "Error post_all_vfun");
    if(errno == EPIPE || errno == ECONNRESET) {
      copy_hist_message = new_msg_data(original_message->hdr.op, original_message->data.buf, original_message->data.hdr.len);
      printf("%s manda il messaggio |%s| a %s: OFFLINE\n", original_message->hdr.sender, original_message->data.buf, curr_user_name);

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
    else {
      (*n_message_sent)++;
    }
  }
  return 0;
}

/**
 * @brief Effettua la lettura da un fd
 *
 * Effettua una ricerca per file descriptor nella tabella hash,
 * acquisisce la lock appropriata ed effettua una o multiple letture sul file descriptor.
 *
 * Se il fd non è presente in tabella hash viene settato il flag fdFound a 0
 * e viene fatta la lettura senza acquisire lock
 *
 * @param ht -- tabella hash
 * @param mux -- mutex dei bucket della tabella
 * @param fd -- file descriptor da cui leggere
 * @param inmsg -- primo messaggio
 * @param inmsg_file -- secondo messaggio in caso di POSTFILE_OP
 * @param fdFound -- flag di output
 *
 * @return Numero di byte letti in caso di successo,
 *         0 (EOF) oppure -1 (errno = ECONNRESET) in caso di disconnessione,
 *         -1 in caso di errore
 * @details Wrapper di icl_hash_apply_until
 */
int read_operation(icl_hash_t *ht, pthread_mutex_t *mux, int fd, message_t *inmsg, message_t *inmsg_file, int *fdFound)
{
  void *argv[5];
  int read_bytes;
  int flag_fdFound = 0;
  argv[0] = (void *) &fd;
  argv[1] = (void *) inmsg;
  argv[2] = (void *) inmsg_file;
  argv[3] = (void *) &flag_fdFound;
  argv[4] = (void *) &read_bytes;

  icl_hash_apply_until(ht, mux, read_operation_vfun, argv);

  if(flag_fdFound == 0) {
    *fdFound = 0;
    printf("Read Operation: FD non trovato\n");
    read_bytes = readMsg(fd, inmsg);

    if(read_bytes == -1)
    {
      printf("Read Operation: Read Invalida, chiudo il fd\n");
      PERROR("read operation");
      SYS(close(fd), -1, "close read operation");
    }
    else if(read_bytes == 0)
    {
      printf("Read Operation: Read = 0, chiudo il fd\n");
      SYS(close(fd), -1, "close read operation");
    }
  }
  else
    *fdFound = 1;

  return read_bytes;
}

/**
 * @brief Effettua una o più letture sul fd dell'utente corrente.
 *
 * Se il fd è stato trovato, ma dopo una o più letture il client corrispondente
 * risulta disconnesso (EOF oppure errno = ECONNRESET), allora il fd viene chiuso.
 *
 * @param corr -- nodo utente corrente
 * @param argv -- puntatore agli argomenti di input-output
 *                (fd da ricercare,
 *                 primo messaggio,
 *                 secondo messaggio in caso di POSTFILE_OP,
 *                 flag fdFound,
 *                 numero di bytes letti)
 * @return 1 in caso l'utente sia stato trovato,
 *         0 altrimenti
 */
int read_operation_vfun(icl_entry_t *corr, void **argv)
{
  user_data_t *curr_user_data = ((user_data_t *) corr->data);

  int fd = *((int*) argv[0]);
  message_t *inmsg = (message_t*) argv[1];
  message_t *inmsg_file = (message_t*) argv[2];

  int read_bytes;
  int read_bytes2;

  if(curr_user_data->fd == fd) {
    printf("Read Operation: FD %d trovato\n", fd);
    read_bytes = readMsg(fd, inmsg);
    *((int*) argv[3]) = 1;
    *((int*) argv[4]) = read_bytes;

    if(read_bytes == -1)
    {
      printf("Read Operation: Read Invalida, chiudo il fd\n");
      perror("read operation");
      SYS(close(fd), -1, "close read operation");
      curr_user_data->fd = -1;
    }
    else if(read_bytes == 0)
    {
      printf("Read Operation: Read = 0, chiudo il fd\n");
      SYS(close(fd), -1, "close read operation");
      curr_user_data->fd = -1;
    }
    else if(read_bytes > 0 && inmsg->hdr.op == POSTFILE_OP)
    {
      read_bytes2 = readData(fd, &inmsg_file->data);
      if(read_bytes2 == -1) {
        printf("Read Operation: Read Invalida, chiudo il fd\n");
        PERROR("read operation");
        SYS(close(fd), -1, "close read operation");
        curr_user_data->fd = -1;
        *((int*) argv[4]) = read_bytes2;
      }
      else if(read_bytes2 == 0) {
        printf("Read Operation: Read = 0, chiudo il fd\n");
        SYS(close(fd), -1, "close read operation");
        curr_user_data->fd = -1;
        *((int*) argv[4]) = read_bytes2;
      }
      else {
        *((int*) argv[4]) = read_bytes + read_bytes2;
      }
    }
    return 1;
  }
  else
    return 0;
}
