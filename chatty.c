/*
 * membox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */

/**
 * @file chatty.c
 * @author Niccolò Cardelli 534015
 * @copyright 2018 Niccolò Cardelli 534015
 * @brief File principale del server chatterbox
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include <sys/select.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "connections.h"
#include "message.h"
#include "ops.h"
#include "stats.h"
#include "config.h"
#include "list.h"
#include "icl_hash.h"
#include "parser.h"
#include "options.h"
#include "scan.h"
#include "group.h"

/* ------------- globali ------------------ */

struct statistics chattyStats = { 0,0,0,0,0,0,0 };
FILE *stat_file;
pthread_mutex_t mux_statistics;

pthread_cond_t cond_taskq;
pthread_mutex_t mux_task_queue;
g_list *task_queue;

int tot_active = 0;
pthread_mutex_t mux_active;

pthread_mutex_t mux_fd;
fd_set set;
fd_set rdset;
int fd_num = 0;
int fd_num_secondo = 1;

pthread_mutex_t *mux_users;
icl_hash_t *users;

g_list *groups;
pthread_mutex_t mux_groups;

struct options ops;

volatile sig_atomic_t eflag = 0;

sigset_t oldmask;
/* ------------- funzioni -------------- */

/**
 * @brief Funzione di uso
 * @param progname -- nome del programma
 */
static void usage(const char *progname)
{
    fprintf(stderr, "Il server va lanciato con il seguente comando:\n");
    fprintf(stderr, "  %s -f conffile\n", progname);
}

/**
 * @brief Crea un pool di thread
 *
 * Alloca un array con tutti gli id dei thread e spawna i thread passando come
 * argomento l'id corrispondente
 *
 * @param numthreads -- numero di thread da spawnare
 * @param fun -- funzione che verra' eseguita dai thread
 * @param arg -- puntatore agli id
 *
 * @return puntatore all'array dei thread
 */
pthread_t * createPool(int numthreads, void * (fun)(void*), int **arg)
{
  pthread_t * pool = (pthread_t *) my_malloc(sizeof(pthread_t) * numthreads);
  int i;
  int * thread_numbers = (int*) my_malloc(sizeof(int) * numthreads);
  for(i=0; i<numthreads; i++) {
    thread_numbers[i] = i+1;
    if(pthread_create(&pool[i], NULL, fun, &thread_numbers[i]) == -1) {
      fprintf(stderr, "Master: CreatePool pthread_create\n");
      return NULL;
    }
  }
  *arg = thread_numbers;
  return pool;
}

/**
 * @brief Funzione fittizia vuota
 *
 * Utilizzata al posto della gestione di default dei segnali
 *
 * @param num -- numero del segnale
 */
void useless_routine(int num)
{}

/**
 * @brief Gestisce i segnali destinati al server
 *
 * @param arg -- numero del thread
 *
 * @return numero del thread
 */
void * handler_worker(void *arg)
{
    sigset_t mask2;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = useless_routine;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);

    sigemptyset(&mask2);
    sigaddset(&mask2, SIGINT);
    sigaddset(&mask2, SIGTERM);
    sigaddset(&mask2, SIGQUIT);
    sigaddset(&mask2, SIGUSR1);
    sigaddset(&mask2, SIGPIPE);

    pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
    int sig;
    do
    {
        sigwait(&mask2, &sig);
        switch (sig)
        {
        case SIGINT:
            printf("Handler: SIGINT %d catturato\n", sig);
            break;
        case SIGQUIT:
            printf("Handler: SIGQUIT %d catturato\n", sig);
            break;
        case SIGTERM:
            printf("Handler: SIGTERM %d catturato\n", sig);
            break;
        case SIGPIPE:
            printf("Handler: SIGPIPE %d catturato\n", sig);
            break;
        case SIGUSR1:
            printf("Handler: SIGUSR1 %d catturato, genero le statistiche\n", sig);
            if (stat_file) {
                printStats(stat_file);
                printStats(stdout);
            }
            break;
        default:
            printf("Handler: ALTRO SEGNALE!\n");
            break;
        }
    } while (sig == SIGUSR1);
    eflag = 1;
    pthread_cond_broadcast(&cond_taskq);
    return arg;
}

/**
 * @brief Funzione dei worker
 *
 * @param arg -- numero del thread
 *
 * @return puntatore al numero del thread
 */
void * worker(void *arg)
{
  int idnumber = *(int*) arg;
  int read1;
  message_t inmsg;
  message_t inmsg2;
  message_t outmsg;
  icl_entry_t *aux;
  int hash1;
  while(!eflag) {

    setHeader(&outmsg.hdr, OP_FAIL, "");
    setData(&outmsg.data, "", NULL, 0);
    setHeader(&inmsg.hdr, OP_FAIL, "");
    setData(&inmsg.data, "", NULL, 0);
    setHeader(&inmsg2.hdr, OP_FAIL, "");
    setData(&inmsg2.data, "", NULL, 0);

    // ---- Estraggo il fd dalla coda ---- //
    printf("Worker[%d]: In attesa di fd in coda..\n", idnumber);
    PTHREAD_MUTEX_LOCK(&mux_task_queue, "muxtaskq");
    while(task_queue->num == 0) {
      pthread_cond_wait(&cond_taskq, &mux_task_queue);
      if(eflag == 1)
          break;
    }
    if(!eflag)
      aux = remove2_g(task_queue, 0);
    PTHREAD_MUTEX_UNLOCK(&mux_task_queue, "muxtaskq");

    if(eflag == 1) {
        printf("Worker[%d]: SEGNALE DI USCITA ricevuto, esco.\n", idnumber);
    }
    else if(aux != NULL) {
        // Prelevo il file descriptor dal task
        int fd = *((int*) aux->key);
        my_free(aux->key);
        my_free(aux);

        printf("Worker[%d]: Un client vuole comunicare\n", idnumber);
        printf("Worker[%d]: Prendo il fd %d dalla coda\n", idnumber, fd);

        int flag_fdFound;
        read1 = read_operation(users, mux_users, fd, &inmsg, &inmsg2, &flag_fdFound);

        if(read1 > 0) {
          switch(inmsg.hdr.op) {
            // ---- Opzione -c: REGISTRAZIONE ---- //
            case REGISTER_OP : {
                printf("Worker[%d]: REGISTER_OP\n", idnumber);

                // La stringa 'sender' inviata dal client è troppo lunga oppure non termina con '\0', mando OP_FAIL
                if (inmsg.hdr.sender[MAX_NAME_LENGTH] != '\0') {
                  printf("Worker[%d]: Nickname troppo lungo\n", idnumber);
                  setHeader(&outmsg.hdr, OP_FAIL, "");
                  SYS(sendHeader(fd, &outmsg.hdr), -1, "sendHeader REGISTER_OP");
                  PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                  updateStats(0, 0, 0, 0, 0, 0, 1);
                  PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");
                }
                else {
                  printf("Worker[%d]: %s vuole registrarsi\n", idnumber, inmsg.hdr.sender);

                  // Preparo la lista degli utenti online in un buffer
                  int i = 0;
                  g_list *list = to_list(users, mux_users);

                  if(get_g(list, inmsg.hdr.sender) == NULL)
                    add_g(list, new_string(inmsg.hdr.sender), NULL);

                  setData(&outmsg.data, "", NULL, (list->num) * (MAX_NAME_LENGTH + 1));
                  outmsg.data.buf = (char*) my_malloc( (list->num) * (MAX_NAME_LENGTH + 1) );
                  memset(outmsg.data.buf, 36, (list->num) * (MAX_NAME_LENGTH + 1));

                  icl_entry_t *corr = list->head;

                  while (corr != NULL) {
                      strncpy(outmsg.data.buf + i * (MAX_NAME_LENGTH + 1), (char*) corr->key, strlen((char*) corr->key) + 1);
                      outmsg.data.buf[(MAX_NAME_LENGTH + 1) * (i + 1) - 1] = '\0';
                      i++;
                      corr = corr->next;
                  }

                  unsigned int hash1 = ((unsigned int) users->hash_function(inmsg.hdr.sender)) % users->nbuckets;
                  printf("Worker[%d]: hash %d\n", idnumber, hash1);
                  int flag_nickAlready;
                  PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "reguser mux");

                  if (icl_hash_find(users, inmsg.hdr.sender))
                  {
                    // Utente precedentemente registrato
                    printf("Worker[%d]: OP_NICK_ALREADY\n", idnumber);
                    setHeader(&outmsg.hdr, OP_NICK_ALREADY, "");
                    SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader REGISTER");
                    flag_nickAlready = 1;
                  }
                  else
                  {
                    // Utente non registrato
                    icl_hash_update_insert(users, new_string(inmsg.hdr.sender), new_usr_data(fd, ops.MaxHistMsgs), NULL);
                    setHeader(&outmsg.hdr, OP_OK, "");
                    SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader REGISTER");
                    SYS(sendData(fd, &outmsg.data), -1, "Errore sendData REGISTER");
                    flag_nickAlready = 0;
                  }

                  PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "reguser mux");

                  PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                  if(flag_nickAlready)
                    updateStats(0, 0, 0, 0, 0, 0, 1);
                  else
                    updateStats(1, 1, 0, 0, 0, 0, 0);
                  PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");

                  destroy_g(list);
                  my_free(outmsg.data.buf);
                }
            } break;
            // ---- Opzione -k: CONNESSIONE ---- //
            case CONNECT_OP : {
                printf("Worker[%d]: %s -> CONNECT_OP\n", idnumber, inmsg.hdr.sender);

                // Preparo la lista degli utenti online in un buffer
                int i = 0;
                g_list *list = to_list(users, mux_users);

                if(get_g(list, inmsg.hdr.sender) == NULL)
                  add_g(list, new_string(inmsg.hdr.sender), NULL);

                icl_entry_t *corr = list->head;

                setData(&outmsg.data, "", NULL, (list->num) * (MAX_NAME_LENGTH + 1));
                outmsg.data.buf = my_malloc((list->num) * (MAX_NAME_LENGTH + 1));
                memset(outmsg.data.buf, 36, (list->num) * (MAX_NAME_LENGTH + 1));

                while (corr != NULL) {
                  strncpy(outmsg.data.buf + i * (MAX_NAME_LENGTH + 1), (char*) corr->key, strlen((char*) corr->key) + 1);
                  outmsg.data.buf[(MAX_NAME_LENGTH + 1) * (i + 1) - 1] = '\0';
                  i++;
                  corr = corr->next;
                }

                unsigned int hash1 = users->hash_function((void*) inmsg.hdr.sender) % users->nbuckets;
                int flag_nickUnknown;
                int flag_alreadyConn = 0;
                icl_entry_t *tmp;
                user_data_t *tmp_data;

                PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)],  "CONNECT_OP mux");

                if((tmp = icl_hash_find(users, inmsg.hdr.sender)) == NULL)
                {
                  // Utente non registrato
                  setHeader(&outmsg.hdr, OP_NICK_UNKNOWN, "");
                  SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader CONNECT_OP");
                  flag_nickUnknown = 1;
                  flag_alreadyConn = 0;
                }
                else
                {
                  // Utente precedentemente registrato
                  flag_nickUnknown = 0;
                  tmp_data = (user_data_t*) tmp->data;
                  printf("Worker[%d]: Vecchio fd %d aggiornato a %d\n", idnumber, tmp_data->fd, fd);
                  if(tmp_data->fd != -1) {
                    printf("Worker[%d]: Utente gia' connesso\n", idnumber);
                    flag_alreadyConn = 1;
                  }
                  else
                    flag_alreadyConn = 0;
                  tmp_data->fd = fd;
                  // Mando OP_OK e la lista al client
                  setHeader(&outmsg.hdr, OP_OK, "");
                  SYS(sendHeader(fd, &outmsg.hdr), -1, "ERRORE sendheader CONNECT_OP");
                  SYS(sendData(fd, &outmsg.data), -1, "ERRORE senddata CONNECT_OP");
                }

                PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "CONNECT_OP mux");

                PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                if(flag_nickUnknown)
                  updateStats(0, 0, 0, 0, 0, 0, 1);
                else if(!flag_alreadyConn)
                  updateStats(0, 1, 0, 0, 0, 0, 0);
                PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");

                destroy_g(list);
                my_free(outmsg.data.buf);
            } break;
            // ---- Opzione -L: RICHIESTA LISTA UTENTI ONLINE ---- //
            case USRLIST_OP : {
                printf("Worker[%d]: %s -> USRLIST_OP\n", idnumber, inmsg.hdr.sender);

                // Preparo la lista degli utenti online in un buffer
                int i = 0;
                g_list *list = to_list(users, mux_users);

                icl_entry_t *corr = list->head;

                setData(&outmsg.data, "", NULL, (list->num) * (MAX_NAME_LENGTH + 1));
                outmsg.data.buf = my_malloc((list->num) * (MAX_NAME_LENGTH + 1));
                memset(outmsg.data.buf, 36, (list->num) * (MAX_NAME_LENGTH + 1));

                while (corr != NULL) {
                  strncpy(outmsg.data.buf + i * (MAX_NAME_LENGTH + 1), (char*) corr->key, strlen((char*) corr->key) + 1);
                  outmsg.data.buf[(MAX_NAME_LENGTH + 1) * (i + 1) - 1] = '\0';
                  i++;
                  corr = corr->next;
                }

                unsigned int hash1 = users->hash_function(inmsg.hdr.sender) % users->nbuckets;

                // Mando OP_OK e la lista al client
                setHeader(&outmsg.hdr, OP_OK, "");
                PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "USRLIST_OP mux");
                SYS(sendHeader(fd, &outmsg.hdr), -1, "ERRORE sendheader USRLIST_OP");
                SYS(sendData(fd, &outmsg.data), -1, "ERRORE senddata USRLIST_OP");
                PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "USRLIST_OP mux");

                destroy_g(list);
                my_free(outmsg.data.buf);
            } break;
            // ---- Opzione -S: INVIO MESSAGGIO ---- //
            case POSTTXT_OP : {
              printf("Worker[%d]: %s -> POSTTXT_OP\n", idnumber, inmsg.hdr.sender);
              printf("Worker[%d]: [%s]: %s -> %s\n", idnumber, inmsg.hdr.sender, inmsg.data.buf, inmsg.data.hdr.receiver);

              unsigned int hash1 = users->hash_function(inmsg.hdr.sender) % users->nbuckets;

              // Controllo che il messaggio non ecceda la dimensione massima
              if (inmsg.data.hdr.len > ops.MaxMsgSize)
              {
                printf("Worker[%d]: Errore OP_MSG_TOOLONG\n", idnumber);

                setHeader(&outmsg.hdr, OP_MSG_TOOLONG, "");
                PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");
                SYS(sendHeader(fd, &outmsg.hdr), -1, "ERRORE sendheader POSTTXT_OP\n");
                PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");

                PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                updateStats(0, 0, 0, 0, 0, 0, 1);
                PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");
              }
              else
              {
                // Il messaggio è valido
                icl_entry_t *rec_group;
                icl_hash_t *group_members;
                int flag_receiver_isGroup;
                int flag_sender_isInGroup;
                int *info;

                PTHREAD_MUTEX_LOCK(&mux_groups, "mux groups");

                // Controllo se il destinatario è nella lista dei gruppi
                rec_group = get_g(groups, inmsg.data.hdr.receiver);

                if(rec_group) {
                  flag_receiver_isGroup = 1;
                  group_members = (icl_hash_t*) rec_group->data;

                  // Controllo se il mittente fa parte del gruppo
                  // e in caso affermativo invio il messaggio a tutti i membri
                  if(icl_hash_find(group_members, inmsg.hdr.sender)) {
                    inmsg.hdr.op = TXT_MESSAGE;
                    info = post_group(users, mux_users, &inmsg, group_members, ops.MaxHistMsgs);
                    flag_sender_isInGroup = 1;
                  }
                  else {
                    flag_sender_isInGroup = 0;
                  }
                }
                else {
                  flag_sender_isInGroup = 0;
                  flag_receiver_isGroup = 0;
                }

                PTHREAD_MUTEX_UNLOCK(&mux_groups, "mux groups");

                if(flag_receiver_isGroup == 1 && flag_sender_isInGroup == 0) {
                  // Il mittente non fa parte del gruppo
                  printf("Worker[%d]: Il mittente non fa parte del gruppo\n", idnumber);
                  setHeader(&outmsg.hdr, OP_NICK_UNKNOWN, "");
                  PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");
                  SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader POSTTXT_OP");
                  PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");

                  PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                  updateStats(0, 0, 0, 0, 0, 0, 1);
                  PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");
                }
                else if(flag_receiver_isGroup == 1 && flag_sender_isInGroup == 1) {
                  // Il destinatario è un gruppo
                  printf("Worker[%d]: Il destinatario è un gruppo\n", idnumber);

                  // Aggiorno le statistiche (messaggi inviati e archiviati)
                  PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                  updateStats(0, 0, info[0], info[1], 0, 0, 0);
                  PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");

                  my_free(info);

                  setHeader(&outmsg.hdr, OP_OK, "");
                  PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");
                  SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader POSTTXT_OP");
                  PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");
                }
                else {
                  // Il destinatario non è un gruppo, ma un utente
                  printf("Worker[%d]: Il destinatario non è un gruppo, ma un utente\n", idnumber);

                  int sendfd;
                  icl_entry_t *rec_user;
                  user_data_t *rec_user_data;
                  int flag_registered_user = 0;
                  int flag_msg_sent;
                  unsigned int hash2 = users->hash_function(inmsg.data.hdr.receiver) % users->nbuckets;

                  setHeader(&outmsg.hdr, TXT_MESSAGE, inmsg.hdr.sender);
                  setData(&outmsg.data, inmsg.data.hdr.receiver, inmsg.data.buf, (unsigned int)inmsg.data.hdr.len);

                  PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash2)], "Errore mux POSTTXT_OP");

                  rec_user = icl_hash_find(users, inmsg.data.hdr.receiver);
                  rec_user_data = ((user_data_t*) rec_user->data);

                  if (rec_user == NULL)
                  {
                    // Utente non registrato
                    flag_registered_user = 0;
                  }
                  else
                  {
                    // Utente registrato
                    flag_registered_user = 1;
                    sendfd = rec_user_data->fd;
                    icl_entry_t *tmp;
                    if (sendfd == -1)
                    {
                      // Utente offline, metto il msg nella history
                      printf("Worker[%d]: ---- UTENTE OFFLINE ----\n", idnumber);
                      add_g(rec_user_data->pvmsg, new_string(inmsg.hdr.sender), new_msg_data(TXT_MESSAGE, inmsg.data.buf, inmsg.data.hdr.len));

                      // Controllo se il numero di messaggi nella history eccede la dimensione massima
                      // e in caso affermativo tolgo il meno recente
                      if((rec_user_data->pvmsg)->num > ops.MaxHistMsgs) {
                        tmp = remove2_g(rec_user_data->pvmsg, 0);
                        my_free(tmp->key);
                        free_data_msg(tmp->data);
                        my_free(tmp);
                      }
                      flag_msg_sent = 0;
                    }
                    else
                    {
                      // Utente in linea, mando il messaggio
                      SYS(sendRequest(sendfd, &outmsg), -1, "Errore sendRequest POSTTXT");
                      if(errno == EPIPE || errno == ECONNRESET)
                      {
                        printf("Worker[%d]: ---- UTENTE NELLA REALTA' OFFLINE ----\n", idnumber);
                        add_g(rec_user_data->pvmsg, new_string(inmsg.hdr.sender), new_msg_data(TXT_MESSAGE, inmsg.data.buf, inmsg.data.hdr.len));

                        // Controllo se il numero di messaggi nella history eccede la dimensione massima
                        // e in caso affermativo tolgo il meno recente
                        if((rec_user_data->pvmsg)->num > ops.MaxHistMsgs) {
                          tmp = remove2_g(rec_user_data->pvmsg, 0);
                          my_free(tmp->key);
                          free_data_msg(tmp->data);
                          my_free(tmp);
                        }
                        rec_user_data->fd = -1;
                        flag_msg_sent = 0;
                      }
                      else
                      {
                        flag_msg_sent = 1;
                        printf("Worker[%d]: ---- UTENTE ONLINE ----\n", idnumber);
                      }
                    }
                  }

                  PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash2)], "Errore mux POSTTXT_OP");

                  if(flag_msg_sent) {
                    PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                    updateStats(0, 0, 1, 0, 0, 0, 0);
                    PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");
                  }
                  else {
                    PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                    updateStats(0, 0, 0, 1, 0, 0, 0);
                    PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");
                  }

                  if (flag_registered_user == 0)
                  {
                    // Utente non registrato prima, mando OP_NICK_UNKNOWN
                    printf("Worker[%d]: Utente non registrato prima, mando OP_NICK_UNKNOWN\n", idnumber);

                    setHeader(&outmsg.hdr, OP_NICK_UNKNOWN, "");
                    PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");
                    SYS(sendHeader(fd, &outmsg.hdr), -1, "ERRORE senddata POSTTXT_OP");
                    PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");

                    PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                    updateStats(0, 0, 0, 0, 0, 0, 1);
                    PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");
                  }
                  else
                  {
                    // Destinatario valido, mando OP_OK
                    printf("Worker[%d]: Destinatario valido, mando OP_OK\n", idnumber);

                    setHeader(&outmsg.hdr, OP_OK, "");
                    PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");
                    SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader POSTTXT_OP");
                    PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");
                  }
                }
              }
              my_free(inmsg.data.buf);
            } break;
            // ---- Opzione -p: RICHIESTA HISTORY DEI MESSAGGI ---- //
            case GETPREVMSGS_OP : {
                printf("Worker[%d]: %s -> GETPREVMSGS_OP\n", idnumber, inmsg.hdr.sender);

                icl_entry_t *rec_user;
                user_data_t *rec_user_data = NULL;
                g_list *pvmsg_list = NULL;

                hash1 = users->hash_function((void *) inmsg.hdr.sender) % users->nbuckets;

                PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux getpvmsg");

                // Prendo il puntatore alla history
                rec_user = icl_hash_find(users, inmsg.hdr.sender);
                rec_user_data = (user_data_t*) rec_user->data;
                pvmsg_list = rec_user_data->pvmsg;

                size_t nummsg = (size_t) pvmsg_list->num;
                printf("Worker[%d]: Numero di messaggi precedenti: %d\n", idnumber, pvmsg_list->num);

                setHeader(&(outmsg.hdr), OP_OK, "");
                setData(&(outmsg.data), "", (char*) &nummsg, sizeof(size_t));
                SYS(sendRequest(fd, &(outmsg)), -1, "Errore sendRequest GETPREVMSGS_OP");

                icl_entry_t *corr;
                msg_data_t *corr_data;
                if (nummsg > 0)
                {
                  corr = pvmsg_list->head;
                  while(corr != NULL)
                  {
                    corr_data = (msg_data_t*) corr->data;
                    printf("Worker[%d]: Messaggio dalla history: %s\n", idnumber, corr_data->buf);
                    setHeader(&outmsg.hdr, corr_data->type, (char*) corr->key);
                    setData(&outmsg.data, inmsg.hdr.sender, corr_data->buf, corr_data->len);
                    SYS(sendRequest(fd, &outmsg), -1, "Errore sendRequest GETPREVMSGS_OP");
                    if(corr_data->type == TXT_MESSAGE) {
                      PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");
                      updateStats(0, 0, 1, -1, 0, 0, 0);
                      PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");
                    }
                    corr = corr->next;
                  }
                  destroy_g(pvmsg_list);
                  rec_user_data->pvmsg = new_glist(string_compare, free_data_msg, my_free);
                }
                PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux getpvmsg");
            } break;
            // ---- Opzione -S: INVIO MESSAGGIO A TUTTI ---- //
            case POSTTXTALL_OP: {
                printf("Worker[%d]: %s -> POSTTXTALL_OP\n", idnumber, inmsg.hdr.sender);
                unsigned int hash1 = users->hash_function(inmsg.hdr.sender) % users->nbuckets;

                // Controllo che il messaggio non ecceda la dimensione massima
                if (inmsg.data.hdr.len > ops.MaxMsgSize)
                {
                  setHeader(&outmsg.hdr, OP_MSG_TOOLONG, "");
                  PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux POSTTXTALL_OP");
                  SYS(sendHeader(fd, &outmsg.hdr), -1, "ERRORE sendheader POSTTXTALL_OP");
                  PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux POSTTXTALL_OP");

                  PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                  updateStats(0, 0, 0, 0, 0, 0, 1);
                  PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");
                }
                else
                {
                  // Messaggio valido
                  int *info;
                  inmsg.hdr.op = TXT_MESSAGE;
                  info = post_all(users, mux_users, &inmsg, ops.MaxHistMsgs);

                  PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                  updateStats(0, 0, info[0], info[1], 0, 0, 0);
                  PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");
                  my_free(info);

                  setHeader(&outmsg.hdr, OP_OK, "");
                  PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux POSTTXTALL_OP");
                  SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader OP_OK POSTTXTALL_OP");
                  PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux POSTTXTALL_OP");
                }
                my_free(inmsg.data.buf);
            } break;
            // ---- Opzione -C: DEREGISTRAZIONE ---- //
            case UNREGISTER_OP : {
                printf("Worker[%d]: %s -> UNREGISTER_OP\n", idnumber, inmsg.hdr.sender);

                hash1 = users->hash_function(inmsg.hdr.sender) % users->nbuckets;
                int esito;
                int read_bytes;
                int read2;
                int flag_fdClosed;

                PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "unreg mux");

                esito = icl_hash_delete(users, inmsg.hdr.sender, my_free, free_data_usr);
                if (esito == -1)
                {
                  // Utente sconosciuto
                  setHeader(&outmsg.hdr, OP_NICK_UNKNOWN, "");
                  SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader UNREGISTER_OP");
                }
                else
                {
                  // Utente registrato
                  setHeader(&outmsg.hdr, OP_OK, "");
                  SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader UNREGISTER_OP");

                  inmsg.data.buf = NULL;
                  read_bytes = readMsg(fd, &inmsg);

                  if(read_bytes == -1)
                  {
                    printf("Worker[%d]: Read Invalida, chiudo il fd\n",idnumber);
                    PERROR("read UNREGISTER_OP");
                    SYS(close(fd), -1, "close read operation");
                    flag_fdClosed = 1;
                  }
                  else if(read_bytes == 0)
                  {
                    printf("Worker[%d]: Read = 0, chiudo il fd\n", idnumber);
                    SYS(close(fd), -1, "close read operation");
                    flag_fdClosed = 1;
                  }
                  else
                    flag_fdClosed = 0;

                  if(inmsg.hdr.op == POSTFILE_OP)
                  {
                    read2 = readData(fd, &inmsg2.data);
                    if(read2 == -1 || read2 == 0) {
                      printf("Worker[%d]: Read = 0 o invalida, chiudo il fd\n", idnumber);
                      SYS(close(fd), -1, "close read operation");
                      flag_fdClosed = 1;
                    }
                    else {
                      flag_fdClosed = 0;
                      my_free(inmsg2.data.buf);
                    }
                  }
                  if(flag_fdClosed == 0) {
                    setHeader(&outmsg.hdr, OP_FAIL, "");
                    SYS(sendHeader(fd, &outmsg.hdr), -1, "sendHeader UNREGISTER_OP");
                  }
                }
                PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "unreg mux");
                if(flag_fdClosed == 1) {
                  PTHREAD_MUTEX_LOCK(&mux_active, "mux_active");
                  tot_active--;
                  PTHREAD_MUTEX_UNLOCK(&mux_active, "mux_active");

                  PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                  updateStats(0, -1, 0, 0, 0, 0, 0);
                  PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");
                }

                my_free(inmsg.data.buf);
                inmsg.hdr.op = UNREGISTER_OP;
                PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                updateStats(-1, 0, 0, 0, 0, 0, 0);
                PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");
            } break;
            // ---- Opzione -s: INVIO FILE ---- //
            case POSTFILE_OP : {
                printf("Worker[%d]: %s -> POSTFILE_OP\n", idnumber, inmsg.hdr.sender);
                printf("Worker[%d]: [%s]: %s -> %s\n", idnumber, inmsg.hdr.sender, inmsg.data.buf, inmsg.data.hdr.receiver);


                unsigned int hash1 = users->hash_function((void *)inmsg.hdr.sender) % users->nbuckets;

                inmsg2.data.buf = realloc(inmsg2.data.buf, sizeof(char) * (inmsg2.data.hdr.len + 1));
                inmsg2.data.buf[inmsg2.data.hdr.len] = '\0';
                printf("Worker[%d]: Dimensione file %d\n", idnumber, inmsg2.data.hdr.len);

                if (inmsg2.data.hdr.len > ops.MaxFileSize)
                {
                  printf("Worker[%d]: Errore MsgTooLong, dimensione %d (max %d)\n", idnumber, inmsg2.data.hdr.len, ops.MaxFileSize);
                  setHeader(&outmsg.hdr, OP_MSG_TOOLONG, "");
                  PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux POSTFILE_OP");
                  SYS(sendHeader(fd, &outmsg.hdr), -1, "Error sendheader POSTFILE_OP");
                  PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux POSTFILE_OP");

                  PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                  updateStats(0, 0, 0, 0, 0, 0, 1);
                  PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");
                }
                else
                {
                  // ---- Il file è valido ----
                  int hash2 = users->hash_function((void *)inmsg.data.hdr.receiver) % users->nbuckets;
                  int i = 0;
                  int guardia = 0;
                  char file_path[MAX_PATH_LENGTH];
                  memset(file_path, 0, sizeof(char) * MAX_PATH_LENGTH);

                  // ---- Compongo il path nel quale creerò il nuovo file ----
                  strcat(file_path, ops.DirName);
                  strcat(file_path, "/");
                  while (inmsg.data.buf[i] != '\0') {
                    if (inmsg.data.buf[i] == '/')
                        guardia = 1;
                    i++;
                  }
                  if (guardia == 1) {
                    inmsg.data.buf = strtok(inmsg.data.buf, "/");
                    char *pointer = strtok(NULL, "");
                    strcat(file_path, pointer);
                  }
                  else
                    strcat(file_path, inmsg.data.buf);
                  printf("Worker[%d]: Path file %s\n", idnumber, file_path);


                  setHeader(&outmsg.hdr, FILE_MESSAGE, inmsg.hdr.sender);
                  setData(&outmsg.data, inmsg.data.hdr.receiver, file_path, strlen(file_path) + 1);

                  FILE *new_file;
                  int flag_isGroup;
                  icl_entry_t *rec_group;
                  int *info;

                  PTHREAD_MUTEX_LOCK(&mux_groups, "mux groups");

                  // ---- Controllo se il destinatario è nella lista dei gruppi ----
                  // ---- e se esiste invio il path del file a tutti i suoi membri ----
                  rec_group = get_g(groups, inmsg.data.hdr.receiver);
                  if(rec_group) {
                    flag_isGroup = 1;
                    SYS(new_file = fopen(file_path, "w+"), NULL, "fopen");
                    int i = 0;
                    while (i < inmsg2.data.hdr.len)
                    {
                        fputc(inmsg2.data.buf[i], new_file);
                        i++;
                    }
                    fclose(new_file);
                    inmsg.hdr.op = FILE_MESSAGE;
                    info = post_group(users, mux_users, &outmsg, (icl_hash_t*) rec_group->data, ops.MaxHistMsgs);
                  }
                  else
                    flag_isGroup = 0;

                  PTHREAD_MUTEX_UNLOCK(&mux_groups, "mux groups");

                  if(flag_isGroup)
                  {
                    // ---- Aggiorno le statistiche (file inviati e archiviati) ----
                    PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                    updateStats(0, 0, 0, 0, info[0], info[1], 0);
                    PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");
                    my_free(info);

                    PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux POSTFILE_OP");
                    setHeader(&outmsg.hdr, OP_OK, "");
                    SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader POSTFILE");
                    PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux POSTFILE_OP");
                  }
                  else
                  {
                    // ---- Il destinatario non è un gruppo, ma un utente
                    icl_entry_t *rec_user;
                    user_data_t *rec_user_data;
                    int sendfd;

                    PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash2)], "mux postfile");
                    if ((rec_user = icl_hash_find(users, inmsg.data.hdr.receiver)) == NULL)
                    {
                      PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash2)], "mux postfile");
                      printf("Worker[%d]: Utente non registrato\n", idnumber);

                      setHeader(&outmsg.hdr, OP_NICK_UNKNOWN, "");
                      PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux POSTFILE_OP");
                      SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader POSTFILE_OP");
                      PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux POSTFILE_OP");

                      PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                      updateStats(0, 0, 0, 0, 0, 0, 1);
                      PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");
                    }
                    else
                    {
                      // ---- Utente registrato ----

                      // ---- Copio il buffer nel file ----
                      SYS(new_file = fopen(file_path, "w+"), NULL, "fopen");
                      int i = 0;
                      while (i < inmsg2.data.hdr.len)
                      {
                          fputc(inmsg2.data.buf[i], new_file);
                          i++;
                      }
                      SYS(fclose(new_file), -1, "fclose postfile");

                      int flag_msg_sent;
                      icl_entry_t *tmp;
                      rec_user_data = (user_data_t*) rec_user->data;

                      if (rec_user_data->fd == -1)
                      {
                        // ---- Utente offline, metto il path del file nella history ----
                        printf("Worker[%d]: ---- UTENTE OFFLINE ----\n", idnumber);
                        add_g(rec_user_data->pvmsg, new_string(inmsg.hdr.sender), new_msg_data(FILE_MESSAGE, file_path, strlen(file_path) + 1));
                        flag_msg_sent = 0;

                        // ---- Controllo se il numero di messaggi nella history eccede la dimensione massima ----
                        // ---- e in caso affermativo tolgo il meno recente ----
                        if((rec_user_data->pvmsg)->num > ops.MaxHistMsgs) {
                          tmp = remove2_g(rec_user_data->pvmsg, 0);
                          my_free(tmp->key);
                          free_data_msg(tmp->data);
                          my_free(tmp);
                        }
                      }
                      else
                      {
                        sendfd = rec_user_data->fd;
                        // ---- Utente in linea, mando il messaggio ----
                        printf("Worker[%d]: ---- UTENTE IN LINEA ----\n", idnumber);
                        SYS(sendRequest(sendfd, &outmsg), -1, "Errore sendRequest POSTFILE");
                        if(errno == EPIPE) {
                          flag_msg_sent = 0;
                          printf("Worker[%d]: ---- UTENTE OFFLINE ----\n", idnumber);
                          add_g(rec_user_data->pvmsg, new_string(inmsg.hdr.sender), new_msg_data(FILE_MESSAGE, file_path, strlen(file_path) + 1));

                          // ---- Controllo se il numero di messaggi nella history eccede la dimensione massima ----
                          // ---- e in caso affermativo tolgo il meno recente ----
                          if((rec_user_data->pvmsg)->num > ops.MaxHistMsgs) {
                            tmp = remove2_g(rec_user_data->pvmsg, 0);
                            my_free(tmp->key);
                            free_data_msg(tmp->data);
                            my_free(tmp);
                          }
                        }
                        else
                          flag_msg_sent = 1;
                      }
                      PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash2)], "mux postfile");

                      if(flag_msg_sent == 0) {
                        PTHREAD_MUTEX_LOCK(&mux_statistics, "mux_statistics POSTFILE_OP");
                        updateStats(0, 0, 0, 0, 0, 1, 0);
                        PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux_statistics POSTFILE_OP");
                      }

                      setHeader(&outmsg.hdr, OP_OK, "");
                      PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux POSTFILE_OP");
                      SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader POSTFILE");
                      PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux POSTFILE_OP");
                    }
                  }
                }
                my_free(inmsg2.data.buf);
                my_free(inmsg.data.buf);
            } break;
            // ---- Opzione: RICHIESTA FILE ---- //
            case GETFILE_OP : {
              printf("Worker[%d]: %s -> GETFILE_OP\n", idnumber, inmsg.hdr.sender);
              printf("Worker[%d]: File richiesto: %s\n", idnumber, inmsg.data.buf);
              FILE *file_d;

              unsigned int hash1 = users->hash_function((void *)inmsg.hdr.sender) % users->nbuckets;
              printf("Worker[%d]: FILE RICHIESTO %s\n", idnumber, inmsg.data.buf);

              if((file_d = fopen(inmsg.data.buf, "r")) == NULL) {
                // Il file richiesto non esiste
                printf("Worker[%d]: Errore file non trovato\n", idnumber);
                setHeader(&outmsg.hdr, OP_NO_SUCH_FILE, "");
                PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux GETFILE_OP");
                SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader GETFILE");
                PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux GETFILE_OP");

                PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                updateStats(0, 0, 0, 0, 0, 0, 1);
                PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");
              }
              else
              {
                // Il file esiste, lo apro e copio il contenuto
                fseek(file_d, 0L, SEEK_END);
                size_t size = ftell(file_d);
                fseek(file_d, 0L, SEEK_SET);
                char * file_content = (char *) my_malloc(size);
                size_t i;
                int k;
                for(i=0, k=0; i<size; i++, k++) {
                    file_content[k] = fgetc(file_d);
                }

                printf("Worker[%d]: File trovato, dimensione file: %d\n", idnumber, (int) size);

                // Invio il file e OP_OK
                setHeader(&outmsg.hdr, OP_OK, "");
                setData(&outmsg.data, "", file_content, (unsigned int)size);

                PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux GETFILE_OP");
                SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader GETFILE");
                SYS(sendData(fd, &outmsg.data), -1, "Errore sendData GETFILE");
                PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux GETFILE_OP");

                PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
                updateStats(0, 0, 0, 0, 1, 0, 0);
                PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");

                SYS(fclose(file_d), -1, "fclose");
                my_free(file_content);
              }
              my_free(inmsg.data.buf);
            } break;
            // ---- Opzione -g: CREAZIONE GRUPPO ---- //
            case CREATEGROUP_OP : {
              printf("Worker[%d]: %s -> CREATEGROUP_OP\n", idnumber, inmsg.hdr.sender);
              printf("Worker[%d]: Nuovo gruppo %s\n", idnumber, inmsg.data.hdr.receiver);
              unsigned int hash1 = users->hash_function((void *)inmsg.hdr.sender) % users->nbuckets;
              icl_entry_t * rec_group;
              PTHREAD_MUTEX_LOCK(&mux_groups, "mux groups");
              rec_group = get_g(groups, (void*) inmsg.data.hdr.receiver);
              if(rec_group)
              {
                setHeader(&outmsg.hdr, OP_NICK_ALREADY, "");
                printf("Worker[%d]: Gruppo gia' creato\n", idnumber);
                PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux CREATEGROUP_OP");
                SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader CREATEGROUP_OP");
                PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux CREATEGROUP_OP");
              }
              else
              {
                icl_hash_t *nu_group = new_group_data();

                // ---- Aggiungo il creatore al gruppo ----
                icl_hash_update_insert(nu_group, new_string(inmsg.hdr.sender), NULL, NULL);

                add_g(groups, new_string(inmsg.data.hdr.receiver), nu_group);

                setHeader(&outmsg.hdr, OP_OK, "");
                PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux CREATEGROUP_OP");
                SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader CREATEGROUP_OP");
                PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux CREATEGROUP_OP");
              }
              PTHREAD_MUTEX_UNLOCK(&mux_groups, "mux groups");
            } break;
            // ---- Opzione -a: AGGIUNTA IN UN GRUPPO ---- //
            case ADDGROUP_OP : {
              printf("Worker[%d]: %s -> ADDGROUP_OP\n", idnumber, inmsg.hdr.sender);
              printf("Worker[%d]: Gruppo a cui aggiungersi %s\n", idnumber, inmsg.data.hdr.receiver);
              unsigned int hash1 = users->hash_function((void *)inmsg.hdr.sender) % users->nbuckets;
              icl_entry_t * rec_group;

              PTHREAD_MUTEX_LOCK(&mux_groups, "mux groups");

              rec_group = get_g(groups, (void*) inmsg.data.hdr.receiver);
              if(rec_group == NULL)
              {
                // Il gruppo non esiste
                PTHREAD_MUTEX_UNLOCK(&mux_groups, "mux groups");
                printf("Worker[%d]: Gruppo sconosciuto!\n", idnumber);
                setHeader(&outmsg.hdr, OP_NICK_UNKNOWN, "");
                PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux CREATEGROUP_OP");
                SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader CREATEGROUP_OP");
                PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux CREATEGROUP_OP");
              }
              else
              {
                // Il gruppo esiste
                icl_entry_t * aux = NULL;
                icl_hash_update_insert((icl_hash_t*) rec_group->data, new_string(inmsg.hdr.sender), NULL, (void**) &aux);
                PTHREAD_MUTEX_UNLOCK(&mux_groups, "mux groups");
                if(aux)
                {
                  // Utente gia' presente nel gruppo
                  printf("Worker[%d]: Utente gia' presente nel gruppo\n", idnumber);
                  my_free(aux->key);
                  my_free(aux);
                  setHeader(&outmsg.hdr, OP_NICK_ALREADY, "");
                  PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux CREATEGROUP_OP");
                  SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader CREATEGROUP_OP");
                  PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux CREATEGROUP_OP");
                }
                else
                {
                  // Tutto ok
                  setHeader(&outmsg.hdr, OP_OK, "");
                  PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux CREATEGROUP_OP");
                  SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader CREATEGROUP_OP");
                  PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux CREATEGROUP_OP");
                }
              }
            } break;
            // ---- Opzione -d: CANCELLAZIONE DA UN GRUPPO ---- //
            case DELGROUP_OP : {
              printf("Worker[%d]: %s -> DELGROUP_OP\n", idnumber, inmsg.hdr.sender);
              printf("Worker[%d]: Gruppo da cui cancellarsi %s\n", idnumber, inmsg.data.hdr.receiver);
              unsigned int hash1 = users->hash_function((void *)inmsg.hdr.sender) % users->nbuckets;
              icl_entry_t * rec_group;

              PTHREAD_MUTEX_LOCK(&mux_groups, "mux groups");
              rec_group = get_g(groups, (void*) inmsg.data.hdr.receiver);
              if(rec_group == NULL)
              {
                // ---- Il gruppo da eliminare non esiste ----
                PTHREAD_MUTEX_UNLOCK(&mux_groups, "mux groups");
                printf("Worker[%d]: Gruppo sconosciuto!\n", idnumber);
                setHeader(&outmsg.hdr, OP_NICK_UNKNOWN, "");
                PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux CREATEGROUP_OP");
                SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader CREATEGROUP_OP");
                PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux CREATEGROUP_OP");
              }
              else
              {
                // ---- Il gruppo da eliminare esiste ----
                int flag_sender_isInGroup;
                flag_sender_isInGroup = icl_hash_delete((icl_hash_t *) rec_group->data, inmsg.hdr.sender, my_free, NULL);
                PTHREAD_MUTEX_UNLOCK(&mux_groups, "mux groups");

                if(flag_sender_isInGroup == -1)
                {
                  // ---- L'utente non e' presente nel gruppo ----
                  printf("Worker[%d]: Utente non presente nel gruppo\n", idnumber);
                  setHeader(&outmsg.hdr, OP_NICK_UNKNOWN, "");
                  PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux CREATEGROUP_OP");
                  SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader CREATEGROUP_OP");
                  PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux CREATEGROUP_OP");
                }
                else
                {
                  // ---- L'utente fa parte del gruppo ----
                  setHeader(&outmsg.hdr, OP_OK, "");
                  PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux CREATEGROUP_OP");
                  SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader CREATEGROUP_OP");
                  PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux CREATEGROUP_OP");
                }
              }
            } break;
            // ---- Opzione Sconosciuta ----
            default : {
              unsigned int hash1 = users->hash_function((void *)inmsg.hdr.sender) % users->nbuckets;
              printf("Worker[%d]: Operazione sconosciuta!\n", idnumber);
              setHeader(&outmsg.hdr, OP_FAIL, "");
              PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux DEFAULT_OP");
              SYS(sendHeader(fd, &outmsg.hdr), -1, "sendHeader DEFAULT");
              PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux DEFAULT_OP");
            } break;
          }
            if(inmsg.hdr.op != UNREGISTER_OP) {
              // ---- Rimetto il fd nel set ---- //s

              PTHREAD_MUTEX_LOCK(&mux_fd, "mux fd");

              printf("Worker[%d]: Rimetto il fd %d nel set\n", idnumber, fd);
              FD_SET(fd, &set);
              if(fd > fd_num_secondo)
                  fd_num_secondo = fd;

              PTHREAD_MUTEX_UNLOCK(&mux_fd, "mux fd");
            }
        }
        else if(flag_fdFound == 1)
        {
          // ---- DISCONNECT_OP ---- ///
          printf("Worker[%d]: DISCONNECT_OP\n", idnumber);
          PTHREAD_MUTEX_LOCK(&mux_active, "mux_active");
          tot_active--;
          PTHREAD_MUTEX_UNLOCK(&mux_active, "mux_active");

          PTHREAD_MUTEX_LOCK(&mux_statistics, "mux statistics");
          updateStats(0, -1, 0, 0, 0, 0, 0);
          PTHREAD_MUTEX_UNLOCK(&mux_statistics, "mux statistics");
        }
    }
    else {
        printf("Worker %d: Nessun fd in coda\n", idnumber);
        sleep(2);
    }
    fflush(stdout);
  }
  return arg;
}

/**
 * @brief main
 *
 * Si occupa di gestire le nuove connessioni, i task e la pulizia delle strutture
 *
 * @param argc -- numero di argomenti del programma
 * @param argv -- argomenti del programma
 */
int main(int argc, char *argv[])
{
    int fd_skt, fd_c;
    int i, aux;
    int desc_rdy = 0;
    struct timeval timeout;
    if(argc != 3) {
        usage("./chatty");
        return -1;
    }

    // ---- Estraggo le opzioni dal file di configurazione ----
    if(parser(argv[2]) == -1) {
        fprintf(stderr, "Master: errore nel parsing\n");
        printf("Master: Using default options\n");
    }
    printOptions();
    fflush(stdout);
    // ---- Maschero tutti i segnali finchè i gestori permanenti non sono istallati ----
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGPIPE);
    if(pthread_sigmask(SIG_BLOCK, &mask, &oldmask)) {
        perror("pthread_sigmask");
        return -1;
    }

    // ----
    printf("ops.StatfileName %s\n", ops.StatFileName);
    SYS(stat_file = fopen(ops.StatFileName, "w"), NULL, "fopen stat_file");

    // ---- Spawno il thread Handler ---- //
    int *hand_thread_id;
    pthread_t * hand = createPool(1, handler_worker, &hand_thread_id);

    // ---- Inizializzo le liste ---- //
    pthread_mutex_init(&mux_task_queue, NULL);
    pthread_cond_init(&cond_taskq, NULL);
    pthread_mutex_init(&mux_fd, NULL);
    pthread_mutex_init(&mux_groups, NULL);
    pthread_mutex_init(&mux_statistics, NULL);
    pthread_mutex_init(&mux_active, NULL);

    task_queue = new_glist(int_compare, NULL, my_free);
    users = icl_hash_create(HASH_TABLE_BUCKETS, NULL, NULL);
    groups = new_glist(string_compare, free_data_group, my_free);
    mux_users = (pthread_mutex_t*) my_malloc(sizeof(pthread_mutex_t) * HASH_TABLE_BUCKETS);

    for(i=0; i<HASH_TABLE_NMUTEX; i++)
        pthread_mutex_init(&mux_users[i], NULL);

    // ---- Creo il pool di thread ---- //
    int * thread_ids;
    pthread_t * pool = createPool(ops.ThreadsInPool, worker, &thread_ids);

    // ---- Creo il socket ---- //
    SYS(fd_skt = socket(AF_UNIX, SOCK_STREAM, 0), -1, "socket");

    // ---- Inizializzo l'indirizzo dove il server sara' in ascolto ---- //
    struct sockaddr_un add;
    memset(&add, '0', sizeof(add));
    add.sun_family = AF_UNIX;
    strncpy(add.sun_path, ops.UnixPath, strlen(ops.UnixPath)+1);

    // ---- Bind e listen ---- //
    unlink(ops.UnixPath);
    SYS(aux = bind(fd_skt, (struct sockaddr *) &add, sizeof(add)), -1, "bind");

    SYS(aux = listen(fd_skt, ops.MaxConnections), -1, "listen");
    int tot_task = 0;
    // ---- Inizializzo set e setto ci inserisco il fd del socket ---- //
    FD_ZERO(&set);
    if(fd_skt > fd_num)
        fd_num = fd_skt;
    FD_SET(fd_skt, &set);
    fd_num_secondo = fd_skt;
    while(!eflag) {
      // ---- Copio il set e il fd massimo aggiornati in quelli che verranno passati alla select ---- //
      PTHREAD_MUTEX_LOCK(&mux_fd, "master mux_fd");
      memcpy(&rdset, &set, sizeof(set));
      fd_num = fd_num_secondo;
      PTHREAD_MUTEX_UNLOCK(&mux_fd, "master mux_fd");

      // ---- Setto il timer della select a 0 ---- //
      timeout.tv_sec = 0;
      timeout.tv_usec = 10000;
      // ---- Se ci sono file descriptor pronti li gestisco, altrimenti la select viene rieseguita ----  //
      if((desc_rdy = select(fd_num+1, &rdset, NULL, NULL, &timeout)) == -1) {
          perror("select");
          fprintf(stderr, "ERROR: facendo la select\n");
          return -1;
      }
      else if(desc_rdy != 0) {
        // ---- desc_rdy file descriptors sono pronti ---- //

        // ---- Scorro tutti i file descriptor ---- //
        for(i=0; i<=fd_num && desc_rdy > 0; i++) {
          if(FD_ISSET(i, &rdset))
          {
            desc_rdy--;
            printf("Master: %d is set\n", i);

            // ---- Se il descrittore pronto è il socket faccio l'accept e lo metto  ---- //
            // ---- nel set, altrimenti passo il fd ai workers e lo tolgo dal set    ---- //
            if(i == fd_skt)
            {
              SYS(fd_c = accept(fd_skt, NULL, 0), -1, "accept");

              int read_bytes;
              int read2;
              int flag_fdClosed;
              message_t tmp;
              message_t tmp2;
              setHeader(&tmp.hdr, OP_FAIL, "");
              setData(&tmp.data, "", NULL, 0);
              setHeader(&tmp2.hdr, OP_FAIL, "");
              setData(&tmp2.data, "", NULL, 0);

              PTHREAD_MUTEX_LOCK(&mux_active, "mux active");
              printf("tot_active %d, ops.MaxConnections %d\n", tot_active, ops.MaxConnections);
              if(tot_active >= ops.MaxConnections) {
                // Troppe connessioni, leggo il task e mando OP_FAIL
                printf("Master: Numero massimo di connessioni raggiunto!\n");
                read_bytes = readMsg(fd_c, &tmp);

                if(read_bytes == -1)
                {
                  printf("Master: Read Invalida, chiudo il fd\n");
                  PERROR("read accept");
                  SYS(close(fd_c), -1, "close accept");
                  flag_fdClosed = 1;
                }
                else if(read_bytes == 0)
                {
                  printf("Master: Read = 0, chiudo il fd\n");
                  SYS(close(fd_c), -1, "close accept");
                  flag_fdClosed = 1;
                }
                else {
                  printf("Master: fd %d di %s, operazione %d rifiutato\n", fd_c, tmp.hdr.sender, tmp.hdr.op);
                  flag_fdClosed = 0;
                }
                if(tmp.hdr.op == POSTFILE_OP)
                {
                  read2 = readData(fd_c, &tmp2.data);
                  if(read2 == -1 || read2 == 0) {
                    printf("Master: Read = 0 o invalida, chiudo il fd\n");
                    SYS(close(fd_c), -1, "close read operation");
                    flag_fdClosed = 1;
                  }
                  else {
                    flag_fdClosed = 0;
                    my_free(tmp2.data.buf);
                  }
                }
                if(flag_fdClosed == 0) {
                  setHeader(&tmp.hdr, OP_FAIL, "");
                  SYS(sendHeader(fd_c, &tmp.hdr), -1, "sendHeader accept");
                  SYS(close(fd_c), -1, "close accept");
                }
                my_free(tmp.data.buf);
                PTHREAD_MUTEX_UNLOCK(&mux_active, "mux active");
              }
              else {
                // Aggiorno il numero di fd attivi
                tot_active++;
                PTHREAD_MUTEX_UNLOCK(&mux_active, "mux active");

                // Aggiungo il fd al set
                PTHREAD_MUTEX_LOCK(&mux_fd, "mux fd");
                FD_SET(fd_c, &set);
                if(fd_c > fd_num_secondo)
                  fd_num_secondo = fd_c;
                PTHREAD_MUTEX_UNLOCK(&mux_fd, "mux fd");
              }
            }
            else
            {
              // ---- Tolgo il fd dal set e aggiorno il fd massimo ----
              PTHREAD_MUTEX_LOCK(&mux_fd, "lock remove from set");
              printf("Master: Tolgo il fd %d dal set\n", i);
              FD_CLR(i, &set);
              if (i == fd_num)
              {
                while (FD_ISSET(fd_num, &rdset) == 0)
                  fd_num_secondo--;
              }
              PTHREAD_MUTEX_UNLOCK(&mux_fd, "lock remove from set");

              // ---- Aggiungo il fd alla coda dei task ----
              PTHREAD_MUTEX_LOCK(&mux_task_queue, "lock task_queue");
              printf("Master: Metto il fd %d in coda\n", i);
              tot_task++;
              add_g(task_queue, new_integer(i), NULL);
              pthread_cond_signal(&cond_taskq);
              PTHREAD_MUTEX_UNLOCK(&mux_task_queue, "lock task_queue");
            }
          }
        }
      }
    }
    printf("\n+------------------------+\n");
    printf("| Terminazione programma |\n");
    printf("+------------------------+\n");
    printf("Totale tasks gestiti %d\n", tot_task);
    printf("Join dei thread worker..\n");
    void **retval = (void **) my_malloc(sizeof(void*)*ops.ThreadsInPool);
    for(i=0; i<ops.ThreadsInPool; i++) {
        pthread_join(pool[i], &retval[i]);
        printf("Worker[%d] ritorna.\n", *(int*) retval[i]);
    }
    my_free(thread_ids);
    my_free(retval);
    printf("Chiudo file delle statistiche ..\n");
    SYS(fclose(stat_file), -1, "fclose stat_file");
    printf("Libero l'array dei thread..\n");
    my_free(pool);
    printf("Libero l'array di mutex..\n");
    my_free(mux_users);
    printf("Immagine della HT:\n");
    icl_hash_dump(stdout, users);
    printf("Distruggo la hash table..\n");
    icl_hash_destroy(users, my_free, free_data_usr);
    destroy_g(groups);
    printf("Distruggo il socket..\n");
    SYS(unlink(ops.UnixPath), -1, "unlink UnixPath");
    printf("Distruggo la coda dei file descriptor..\n");
    destroy_g(task_queue);
    printf("Join del thread handler..\n");
    pthread_join(hand[0], NULL);
    my_free(hand_thread_id);
    my_free(hand);
    return 0;
}
