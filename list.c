/*
 * chatterbox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */
/**
 * @file list.c
 * @author Niccolò Cardelli 534015
 * @copyright 2018 Niccolò Cardelli 534015
 * @brief Contiene le funzioni di utilita' che agiscono su una lista generica
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>

#include "list.h"
#include "config.h"

/* ---- Funzioni di pulizia della memoria ---- */

/**
 * @brief Libera la memoria allocata per un utente (history dei messaggi)
 *
 * @param arg -- puntatore alla struct user_data_t
 */
void free_data_usr(void *arg)
{
  user_data_t *aux = (user_data_t*) arg;
  if(aux == NULL)
      return;
  if(aux->pvmsg != NULL)
    destroy_g(aux->pvmsg);
  my_free(aux);
}

/**
 * @brief Libera la memoria allocata per un messagio della history (buf)
 *
 * @param arg -- puntatore alla struct msg_data_t
 */
void free_data_msg(void *arg)
{
  msg_data_t *msg = (msg_data_t*) arg;
  if(msg == NULL)
    return;
  my_free((void*)(msg->buf));
  my_free((void*) msg);
}


/* ---- Funzioni di creazione ---- */

/**
 * @brief Crea una nuova lista generica
 *
 * @param max -- dimensione massima della lista
 * @param compare -- funzione di confronto
 * @param free_data -- funzione per liberare la parte dati del nodo
 * @param free_key -- funzione per liberare la chiave del nodo
 *
 * @return puntatore alla nuova lista
 */
g_list * new_glist(int (*compare)(void *, void *), void (*free_data)(void *), void (*free_key)(void *))
{
  g_list *nu_list = (g_list*) my_malloc(sizeof(g_list));
  nu_list->head = NULL;
  nu_list->tail = NULL;
  nu_list->num = 0;
  nu_list->compare = compare;
  nu_list->free_data = free_data;
  nu_list->free_key = free_key;
  return nu_list;
}

/**
 * @brief Alloca dinamicamente un intero e lo restituisce
 *
 * @param -- integer l'intero
 *
 * @return puntatore al nuovo intero
 */
int * new_integer(int integer)
{
  int *nu_int = (int*) my_malloc(sizeof(int));
  *(nu_int) = integer;
  return nu_int;
}

/**
 * @brief Alloca dinamicamente una stringa e la restituisce
 *
 * @param string -- stringa
 *
 * @return puntatore alla nuova stringa
 */
char * new_string(char *string)
{
  if(string == NULL)
    return NULL;
  char *nu_str = (char*) my_malloc((strlen(string) + 1));
  memset(nu_str, 0, strlen(string) + 1);
  strncpy(nu_str, string, strlen(string));
  nu_str[strlen(string)] = '\0';
  return nu_str;
}

/**
 * @brief Crea un nuovo utente
 *
 * @param fd -- file descriptor
 * @param max -- dimensione massima della history
 *
 * @return puntatore alla struct del nuovo utente
 */
user_data_t * new_usr_data(int fd, int max)
{
  user_data_t *nu_user = (user_data_t*) my_malloc(sizeof(user_data_t));
  nu_user->fd = fd;
  nu_user->pvmsg = new_glist(string_compare, free_data_msg, my_free);
  if(nu_user->pvmsg == NULL)
    return NULL;
  return nu_user;
}

/**
 * @brief Crea un nuovo messaggio
 *
 * @param type -- tipo del messaggio (TXT_MESSAGE, FILE_MESSAGE)
 * @param buf -- messaggio
 * @param len -- lunghezza del messaggio
 *
 * @return puntatore al nuovo messaggio
 */
msg_data_t * new_msg_data(op_t type, char *buf, int len)
{
  msg_data_t *nu_msg = (msg_data_t*) my_malloc(sizeof(msg_data_t));
  memset(nu_msg, 0, sizeof(msg_data_t));
  nu_msg->type = type;
  nu_msg->len = len;
  nu_msg->buf = new_string(buf);
  return nu_msg;
}

/* ---- Funzioni principali ---- */

/**
 * @brief Elimina una lista generica
 *
 * @param list -- lista da distruggere
 */
void destroy_g(g_list *list)
{
  icl_entry_t *aux = NULL;
  if(list == NULL)
    return;
  if(list->num <= 0){
    my_free(list);
    list = NULL;
    return;
  }
  while((aux = remove2_g(list, 0)) != NULL){
    if((list)->free_data != NULL){
        (list)->free_data(aux->data);
        aux->data = NULL;
    }
    if((list)->free_key != NULL){
        (list)->free_key(aux->key);
        aux->key = NULL;
    }
    my_free(aux);
  }
  my_free(list);
  list = NULL;
}

/**
 * @brief Aggiunge un elemento ad una lista generica
 *        (ed elimina il nodo in testa se si supera il numero massimo)
 *
 * @param list -- lista
 * @param key -- chiave del nodo
 * @param data -- parte dati del nodo
 */
void add_g(g_list *list, void *key, void *data)
{
  icl_entry_t *nu_element = (icl_entry_t*) my_malloc(sizeof(icl_entry_t));

  nu_element->next = NULL;
  nu_element->key = key;
  nu_element->data = data;

  if(list->tail == NULL) {
    list->tail = nu_element;
    list->head = nu_element;
  }
  else {
    list->tail->next = nu_element;
    list->tail = nu_element;
  }
  (list->num)++;
}

/**
 * @brief Elimina un elemento dalla lista (per chiave)
 *
 * @param list -- lista
 * @param key -- chiave dell'elemento
 *
 * @return puntatore al nodo rimosso, NULL se non e' stato trovato
 */
icl_entry_t * remove_g(g_list *list, void *key)
{
  icl_entry_t *corr = list->head;
  icl_entry_t *prec = NULL;
  while(corr != NULL) {
    if(list->compare(corr->key, key)) {
      if(prec == NULL)
        list->head = corr->next;
      else
        prec->next = corr->next;
      if(corr->next == NULL)
        list->tail = NULL;
      return corr;
      (list->num)--;
    }
  }
  return NULL;
}

/**
 * @brief Elimina un elemento dalla lista (per indice)
 *
 * @param list -- lista
 * @param pos -- indice del nodo da eliminare
 *
 * @return puntatore al nodo rimosso, NULL se non e' stato trovato
 */
icl_entry_t * remove2_g(g_list *list, int pos)
{
  int i = 0;
  if(list == NULL)
    return NULL;
  if(list->num == 0)
    return NULL;
  icl_entry_t *corr = list->head;
  icl_entry_t *prec = NULL;
  while(corr != NULL) {
    if(i == pos) {
      if(prec == NULL) {
        list->head = corr->next;
        if(corr->next == NULL)
          list->tail = NULL;
      }
      else {
        prec->next = corr->next;
        if(corr->next == NULL)
          list->tail = prec;
      }
      (list->num)--;
      corr->next = NULL;
      return corr;
    }
    else {
      i++;
      prec = corr;
      corr = corr->next;
    }
  }
  return NULL;
}

/**
 * @brief Cerca un nodo nella lista in base alla chiave
 *
 * @param list -- lista
 * @param arg -- chiave da confrontare
 *
 * @return puntatore al nodo, NULL se non e' stato trovato
 */
icl_entry_t * get_g(g_list *list, void *arg)
{
  icl_entry_t *corr = list->head;
  while(corr != NULL) {
    if(list->compare(arg, corr->key))
        return corr;
    else {
        corr = corr->next;
    }
  }
  return corr;
}
