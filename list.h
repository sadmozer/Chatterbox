/*
 * membox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */

/**
 * @file list.h
 * @author Niccolò Cardelli 534015
 * @copyright 2018 Niccolò Cardelli 534015
 * @brief Contiene le definizioni di strutture di una lista generica e
 *        le dichiarazioni delle relative funzioni di utilita' definite in list.c
 */

#include <pthread.h>

#include "ops.h"
#include "icl_hash.h"

#if !defined (LIST_H_)
#define LIST_H_


/**
 * @struct g_list
 * @brief lista generica
 *
 * @var head -- puntatore al primo elemento della lista
 * @var tail -- puntatore all'ultimo elemento della lista
 * @var num -- numero di elementi nella lista
 * @var compare -- funzione di confronto
 * @var free_key -- funzione per liberare la memoria della chiave
 * @var free_data -- funzione per liberare la memoria dei dati
 */
typedef struct g_list{
    icl_entry_t *head;
    icl_entry_t *tail;
    int num;
    int (*compare)(void *, void *);
    void (*free_key)(void *);
    void (*free_data)(void *);
} g_list;

/**
 * @struct user_data_t
 * @brief dati di un utente
 *
 * @var fd -- file descriptor
 * @var pvmsg -- puntatore alla lista dei messaggi precedenti (history)
 * @details se il fd dell'utente e' -1 esso si considera OFFLINE,
 *          se >=0 si considera ONLINE
 */
typedef struct user_data_t {
        int fd;
        g_list * pvmsg;
} user_data_t;

/**
 * @struct msg_data_t
 * @brief messaggio conservato nella history
 *
 * @var type -- tipo di messaggio (TXT_MESSAGE, FILE_MESSAGE)
 * @var buf -- messaggio
 * @var len -- lunghezza del messaggio
 */
typedef struct msg_data_t
{
    op_t type;
    char *buf;
    int len;
} msg_data_t;

char * new_string(char *string);
int * new_integer(int integer);
g_list * new_glist(int (*compare)(void *, void *), void (*free_data)(void *), void (*free_key)(void *));
user_data_t * new_usr_data(int fd, int max);
msg_data_t * new_msg_data(op_t type, char *buf, int len);

void free_data_usr(void *arg);
void free_data_msg(void *arg);

void destroy_g(g_list *list);
void add_g(g_list *list, void *key, void *data);
icl_entry_t * remove_g(g_list *list, void *key);
icl_entry_t * remove2_g(g_list *list, int pos);
icl_entry_t * get_g(g_list * list, void *arg);

#endif
