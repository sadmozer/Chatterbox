#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include "message.h"
#include "icl_hash.h"
#include "nuovo.h"
#include "list.h"
#include "connections.h"
#include "config.h"
#include "stats.h"

/**
* @file nuovo.c
* @author Niccolò Cardelli
* @date 8 Mar 2018
* @copyright 2018 Niccolò Cardelli
* @brief <brief>
*
*/

/**
* @function to_list
* @brief Genera la lista degli utenti connessi
*
* @param tab tabella hash
* @param mux mutex dei bucket della tabella
*
* @return lista di utenti
* @details Wrapper di icl_hash_apply
*/
g_list * to_list(icl_hash_t * tab, pthread_mutex_t * mux){
  g_list * nu_list = new_glist(tab->hash_key_compare, NULL, free_pointer);
  icl_hash_apply_anyway(tab, mux, to_list_vfun, (void **) &nu_list);
  return nu_list;
}

/**
* @function to_list_vfun
* @brief Aggiunge l'utente corrente alla lista degli utenti connessi
*
* @param corr nodo utente corrente
* @param argv puntatore agli argomenti di input-output
*
* @return 0
* @details dato che restituisce sempre 0, si effettua una scansione
*          completa della tabella hash
*/
int to_list_vfun(icl_entry_t * corr, void ** argv){
  user_data_t * curr_user_data = ((user_data_t *) corr->data);
  char * curr_user_name = (char*) corr->key;
  if(curr_user_data->fd != -1)
    add_g((g_list*)(*argv), new_string(curr_user_name), NULL);
  return 0;
}

/**
* @function
* @brief
* @param [in] <name> <parameter_description>
* @return <return_description>
* @details <details>
*/
void post_all(icl_hash_t * ht, pthread_mutex_t * mux, char * sender, msg_data_t * hist_message){
    message_t sendr_message;
    setHeader(&sendr_message.hdr, hist_message->type, sender);
    setData(&sendr_message.data, "", hist_message->buf, hist_message->len);
    void * argv[4];
    argv[0] = (void *) ht;
    argv[1] = (void *) sender;
    argv[2] = (void *) hist_message;
    argv[3] = (void *) &sendr_message;
    icl_hash_apply_anyway(ht, mux, post_all_vfun, argv);
    free_data_msg(hist_message);
}
int post_all_vfun(icl_entry_t * corr, void ** argv){
    icl_hash_t * ht = (icl_hash_t*) argv[0];
    char * sender = (char*) argv[1];
    msg_data_t * original_hist_message = (msg_data_t*) argv[2];
    message_t * send_message = (message_t*) argv[3];

    msg_data_t * copy_hist_message;
    user_data_t * curr_user_data = ((user_data_t *) corr->data);
    char * curr_user_name = (char*) corr->key;

    if (ht->hash_key_compare(curr_user_name, sender) == 0)
    {
      copy_hist_message = new_msg_data(original_hist_message->type, original_hist_message->buf, original_hist_message->len);
      if (curr_user->fd == -1)
        {
          printf("%s manda il messaggio |%s| a %s: OFFLINE\n", sender, copy_hist_message->buf, curr_user_name);
          add_g(curr_user->pvmsg, new_string(sender), copy_hist_message);
          updateStats(0, 0, 0, 1, 0, 0, 0);
        }
        else
        {
            if (curr_user->fd > 100 || curr_user->fd < 0)
                printf("C'è qualcosa che non va\n");
            printf("%s manda il messaggio |%s| a %s: ONLINE %d\n", sender, copy_hist_message->buf, (char *)corr->key, ((user_data_t *)corr->data)->fd);
            SYS(sendRequest(curr_user->fd, send_message), -1, "Error icl_hash_postall");
            if(errno == EPIPE){
              printf("%s manda il messaggio |%s| a %s: OFFLINE\n", sender, copy_hist_message->buf, curr_user_name);
              add_g(curr_user->pvmsg, new_string(sender), copy_hist_message);
              updateStats(0, 0, 0, 1, 0, 0, 0);
            }
            else
                free_data_msg(copy_hist_message);
        }
    }
    return 0;
}
char * set_offline(icl_hash_t * ht, pthread_mutex_t * mux, int fd){
    char * username = NULL;
    void * argv[2];
    argv[0] = (void *) &fd;
    argv[1] = (void *) &username;
    icl_hash_apply_anyway(ht, mux, set_offline_vfun, argv);
    return username;
}
int set_offline_vfun(icl_entry_t * corr, void **argv){
    int fd = *((int*) (argv[0]));
    char ** username = (char**) (argv[1]);

    user_data_t * curr_user_data = ((user_data_t *)corr->data);
    char * curr_user_name = (char*) corr->key;

    printf("%s, fd = %d\n", curr_user_name, curr_user_data->fd);
    if (curr_user_data->fd == fd)
    {
      SYS(close(fd), -1, "close");
      curr_user_data->fd = -1;
      if (curr_user_name != NULL)
        (*username) = new_string(curr_user_name);
      return 1;
    }
    else
        return 0;
}
