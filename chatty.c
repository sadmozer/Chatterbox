/*
 * membox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */
/**
 * @file chatty.c
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

/* inserire gli altri include che servono */
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
#include "nuovo.h"

/* struttura che memorizza le statistiche del server, struct statistics
 * e' definita in stats.h.
 *
 */
/* ------------- define -----------------*/
/* -------------- macro -----------------*/
/* ------------- enum ------------------ */
/* ------------- struct ------------------ */
/* ------------- globali ------------------ */
struct statistics  chattyStats = { 0,0,0,0,0,0,0 };
FILE * stat_file;
pthread_cond_t cond_taskq;
pthread_mutex_t mux_task_queue;
g_list * task_queue;

pthread_mutex_t mux_fd;
fd_set set;
fd_set rdset;

pthread_mutex_t * mux_users;
icl_hash_t * users;

pthread_mutex_t mux_groups;
struct options ops;

pthread_mutex_t mux_conn_map;
icl_hash_t * conn_map;
int fd_num = 0;

volatile sig_atomic_t eflag = 0;

sigset_t oldmask;

/* ------------- funzioni -------------- */
static void usage(const char *progname) {
    fprintf(stderr, "Il server va lanciato con il seguente comando:\n");
    fprintf(stderr, "  %s -f conffile\n", progname);
}
void handler(int signum){
}
void * handler_worker(void * arg){
    sigset_t mask;
    sigset_t mask2;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
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
            if (stat_file)
                printStats(stat_file);
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

void * worker(void * arg){
    int idnumber = *(int*) arg;
    int read1;
    message_t inmsg;
    message_t outmsg;
    icl_entry_t * aux;
    int hash1;
    while(!eflag){

        setHeader(&outmsg.hdr, OP_FAIL, "");
        setData(&outmsg.data, "", NULL, (unsigned int) 0);
        setHeader(&inmsg.hdr, OP_FAIL, "");
        setData(&inmsg.data, "", NULL, (unsigned int) 0);

        // ---- Estraggo il fd dalla coda ---- //
        printf("Worker[%d]: In attesa di fd in coda..\n", idnumber);
        PTHREAD_MUTEX_LOCK(&mux_task_queue, "muxtaskq");
        while(task_queue->num == 0){
            pthread_cond_wait(&cond_taskq, &mux_task_queue);
            if(eflag == 1)
                break;
        }
        if(!eflag)
            aux = remove2_g(task_queue, 0);
        PTHREAD_MUTEX_UNLOCK(&mux_task_queue, "muxtaskq");

        if(eflag == 1){
            printf("Worker[%d]: SEGNALE DI USCITA ricevuto, esco.\n", idnumber);
            continue;
        }
        if(aux != NULL){
            int fd = *((int*) aux->key);
            printf("Worker[%d]: Un client vuole comunicare\n", idnumber);
            printf("Worker[%d]: Prendo il fd %d dalla coda\n", idnumber, fd);
            SYS(read1 = readMsg(fd, &inmsg), -1, "read task");
            free_pointer(aux->key);
            my_free(aux);
            if(read1 > 0) {
                switch(inmsg.hdr.op) {
                // ---- Opzione -c: REGISTRAZIONE ----
                case REGISTER_OP : {
                    printf("Worker[%d]: %s -> REGISTER OP\n", idnumber, inmsg.hdr.sender);
                    // La stringa 'sender' inviata dal client è troppo lunga oppure non termina con '\0', mando OP_FAIL
                    if (inmsg.hdr.sender[MAX_NAME_LENGTH] != '\0' && strlen(inmsg.hdr.sender) >= MAX_NAME_LENGTH + 1) {
                        setHeader(&outmsg.hdr, OP_FAIL, "");
                        SYS(sendHeader(fd, &outmsg.hdr), -1, "sendHeader REGISTER_OP");
                        updateStats(0, 0, 0, 0, 0, 0, 1);
                    }
                    else {
                        // Preparo la lista degli utenti online in un buffer
                        g_list *list = to_list(users, mux_users);
                        icl_entry_t *corr = list->head;

                        setData(&outmsg.data, "", NULL, (list->num + 1) * (MAX_NAME_LENGTH + 1));
                        if((outmsg.data.buf = (char *) my_malloc( (list->num + 1) * (MAX_NAME_LENGTH + 1) )) == NULL)
                            perror("my_malloc");
                        memset(outmsg.data.buf, 36, (list->num + 1) * (MAX_NAME_LENGTH + 1));

                        // Inserisco l'utente che ha fatto richiesta
                        strncpy(outmsg.data.buf, inmsg.hdr.sender, strlen(inmsg.hdr.sender) + 1);

                        // Inserisco tutti gli altri
                        int i = 1;
                        while (corr != NULL) {
                            strncpy(outmsg.data.buf + i * (MAX_NAME_LENGTH + 1), (char *) corr->key, strlen((char *) corr->key) + 1);
                            outmsg.data.buf[(MAX_NAME_LENGTH + 1) * (i + 1) - 1] = '\0';
                            i++;
                            corr = corr->next;
                        }

                        unsigned int hash1 = ((unsigned int) users->hash_function((void *)inmsg.hdr.sender)) % users->nbuckets;
                        printf("Worker[%d]: hash %d\n", idnumber, hash1);

                        user_data_t *olddata = NULL;
                        PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "reguser mux");

                        if ((olddata = icl_hash_find(users, (void*) inmsg.hdr.sender)))
                        {
                            // Utente precedentemente registrato, aggiorno il suo stato e mando OP_NICK_ALREADY
                            printf("Worker[%d]: OP_NICK_ALREADY\n", idnumber);
                            // printf("Worker[%d]: Vecchio fd %d aggiornato a %d\n", idnumber, olddata->fd, fd);
                            // // olddata->status = 1;
                            // olddata->fd = fd;
                            setHeader(&outmsg.hdr, OP_NICK_ALREADY, "");
                            SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader REGISTER");
                            updateStats(0, 0, 0, 0, 0, 0, 1);
                        }
                        else
                        {
                            icl_entry_t * oldnode = NULL;
                            // Utente non registrato prima
                            icl_hash_update_insert(users, (void *) new_string(inmsg.hdr.sender), (void *) new_usr_data(fd, ops.MaxHistMsgs), (void**) &oldnode);
                            if(oldnode)
                                printf("Worker[%d]: ERRORE REGISTER\n", idnumber);
                            // Mando OP_OK e mando la lista
                            setHeader(&outmsg.hdr, OP_OK, "");
                            SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader REGISTER");
                            SYS(sendData(fd, &outmsg.data), -1, "Errore sendData REGISTER");
                            updateStats(1, 1, 0, 0, 0, 0, 0);
                        }

                        PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "reguser mux");

                        destroy_g(list);
                        my_free(outmsg.data.buf);
                    }
                } break;
                // ---- Opzione -k: CONNESSIONE ----
                case CONNECT_OP : {
                    printf("Worker[%d]: %s -> CONNECT_OP\n", idnumber, inmsg.hdr.sender);

                    // Preparo la lista degli utenti online in un buffer
                    g_list *list = to_list(users, mux_users);
                    icl_entry_t *corr = list->head;

                    setData(&outmsg.data, "", NULL, (list->num + 1) * (MAX_NAME_LENGTH + 1));
                    if ((outmsg.data.buf = (char *) my_malloc((list->num + 1) * (MAX_NAME_LENGTH + 1))) == NULL)
                        perror("my_malloc");
                    memset(outmsg.data.buf, 36, (list->num + 1) * (MAX_NAME_LENGTH + 1));

                    // Inserisco l'utente che ha fatto richiesta
                    strncpy(outmsg.data.buf, inmsg.hdr.sender, strlen(inmsg.hdr.sender) + 1);

                    // Inserisco tutti gli altri
                    int i = 1;
                    while (corr != NULL){
                        strncpy(outmsg.data.buf + i * (MAX_NAME_LENGTH + 1), (char *)corr->key, strlen((char *)corr->key) + 1);
                        outmsg.data.buf[(MAX_NAME_LENGTH + 1) * (i + 1) - 1] = '\0';
                        i++;
                        corr = corr->next;
                    }

                    unsigned int hash1 = users->hash_function((void *) inmsg.hdr.sender) % users->nbuckets;

                    PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)],  "CONNECT_OP mux");
                    user_data_t * ax = icl_hash_find(users, (void *)inmsg.hdr.sender);
                    if(ax == NULL)
                    {
                        // Non si è registrato prima
                        setHeader(&outmsg.hdr, OP_NICK_UNKNOWN, "");
                        SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader CONNECT_OP");
                        PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "CONNECT_OP mux");
                        updateStats(0, 0, 0, 0, 0, 0, 1);
                    }
                    else
                    {
                        if(ax->fd == -1){
                            updateStats(0, 1, 0, 0, 0, 0, 0);
                            // printf("Worker[%d]: Utente passa da OFFLINE a ONLINE\n", idnumber);
                        }
                        // ax->status = 1;
                        printf("Worker[%d]: Vecchio fd %d aggiornato a %d\n", idnumber, ax->fd, fd);
                        ax->fd = fd;
                        // Mando OP_OK e la lista al client
                        setHeader(&outmsg.hdr, OP_OK, "");
                        SYS(sendHeader(fd, &outmsg.hdr), -1, "ERRORE sendheader CONNECT_OP");
                        SYS(sendData(fd, &outmsg.data), -1, "ERRORE senddata CONNECT_OP");
                        PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "CONNECT_OP mux");
                    }
                    destroy_g(list);
                    my_free(outmsg.data.buf);
                } break;
                // ---- Opzione -L: RICHIESTA LISTA UTENTI ONLINE ----
                case USRLIST_OP : {
                    printf("Worker[%d]: %s -> USRLIST_OP\n", idnumber, inmsg.hdr.sender);

                    // Preparo la lista degli utenti online in un buffer
                    g_list *list = to_list(users, mux_users);
                    icl_entry_t *corr = list->head;

                    setData(&outmsg.data, "", NULL, (list->num + 1) * (MAX_NAME_LENGTH + 1));
                    if ((outmsg.data.buf = (char *)my_malloc((list->num + 1) * (MAX_NAME_LENGTH + 1))) == NULL)
                        perror("my_malloc");
                    memset(outmsg.data.buf, 36, (list->num + 1) * (MAX_NAME_LENGTH + 1));

                    // Inserisco l'utente che ha fatto richiesta
                    strncpy(outmsg.data.buf, inmsg.hdr.sender, strlen(inmsg.hdr.sender) + 1);

                    // Inserisco tutti gli altri
                    int i = 1;
                    while (corr != NULL){
                        strncpy(outmsg.data.buf + i * (MAX_NAME_LENGTH + 1), (char *) corr->key, strlen((char *) corr->key) + 1);
                        outmsg.data.buf[(MAX_NAME_LENGTH + 1) * (i + 1) - 1] = '\0';
                        i++;
                        corr = corr->next;
                    }
                    unsigned int hash1 = users->hash_function((void *) inmsg.hdr.sender) % users->nbuckets;

                    // Mando OP_OK e la lista al client
                    setHeader(&outmsg.hdr, OP_OK, "");
                    PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "USRLIST_OP mux");
                    SYS(sendHeader(fd, &outmsg.hdr), -1, "ERRORE sendheader USRLIST_OP");
                    SYS(sendData(fd, &outmsg.data), -1, "ERRORE senddata USRLIST_OP");
                    PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "USRLIST_OP mux");

                    destroy_g(list);
                    my_free(outmsg.data.buf);
                } break;
                // ---- Opzione -S: INVIO MESSAGGIO ----
                case POSTTXT_OP : {
                    printf("Worker[%d]: %s -> POSTTXT_OP\n", idnumber, inmsg.hdr.sender);
                    printf("Worker[%d]: [%s]: %s -> %s\n", idnumber, inmsg.hdr.sender, inmsg.data.buf, inmsg.data.hdr.receiver);

                    unsigned int hash1 = users->hash_function((void *) inmsg.hdr.sender) % users->nbuckets;

                    // ---- Controllo che il destinatario del messaggio sia diverso dal mittente ----
                    // if (strcmp(inmsg.hdr.sender, inmsg.data.hdr.receiver) == 0)
                    // {
                    //     printf("Worker[%d]: Errore Ricevente uguale a destinatario\n", idnumber);
                    //     setHeader(&outmsg.hdr, OP_FAIL, "");
                    //     PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");
                    //     SYS(sendHeader(fd, &outmsg.hdr), -1, "ERRORE sendheader POSTTXT_OP\n");
                    //     PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");
                    //     updateStats(0, 0, 0, 0, 0, 0, 1);
                    // } // ---- Controllo che il messaggio non ecceda la dimensione massima ----
                    // else
                    if (inmsg.data.hdr.len > ops.MaxMsgSize)
                    {
                        printf("Worker[%d]: Errore OP_MSG_TOOLONG\n", idnumber);
                        setHeader(&outmsg.hdr, OP_MSG_TOOLONG, "");
                        PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");
                        SYS(sendHeader(fd, &outmsg.hdr), -1, "ERRORE sendheader POSTTXT_OP\n");
                        PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");
                        updateStats(0, 0, 0, 0, 0, 0, 1);
                    }
                    else
                    {
                        // ---- Il messaggio è valido ----
                        unsigned int hash2 = users->hash_function((void *) inmsg.data.hdr.receiver) % users->nbuckets;
                        unsigned int lock_target2 = hash_lock(hash2);
                        int sendfd = 0;
                        user_data_t *ax = NULL;
                        int esito = -1;
                        setHeader(&outmsg.hdr, TXT_MESSAGE, inmsg.hdr.sender);
                        setData(&outmsg.data, inmsg.data.hdr.receiver, inmsg.data.buf, (unsigned int)inmsg.data.hdr.len);

                        PTHREAD_MUTEX_LOCK(&mux_users[lock_target2], "Errore mux POSTTXT_OP");

                        // ---- Cerco l'utente ----
                        ax = icl_hash_find(users, (void *)inmsg.data.hdr.receiver);

                        if (ax != NULL)
                        {
                        // ---- L'utente esiste ----
                            if (ax->fd == -1)
                            {
                                // ---- Utente offline, metto il msg nella history ----
                                printf("Worker[%d]: ---- UTENTE OFFLINE ----\n", idnumber);
                                add_g(ax->pvmsg, new_string(inmsg.hdr.sender), new_msg_data(TXT_MESSAGE, inmsg.data.buf, inmsg.data.hdr.len));
                                updateStats(0, 0, 0, 1, 0, 0, 0);
                            }
                            else
                            {
                                sendfd = ax->fd;
                                // ---- Utente in linea, mando il messaggio ----
                                printf("Worker[%d]: ---- UTENTE ONLINE ----\n", idnumber);
                                SYS(sendRequest(sendfd, &outmsg), -1, "Errore sendRequest POSTTXT");
                                if(errno == EPIPE){
                                    printf("Worker[%d]: ---- UTENTE NELLA REALTA' OFFLINE ----\n", idnumber);
                                    add_g(ax->pvmsg, new_string(inmsg.hdr.sender), new_msg_data(TXT_MESSAGE, inmsg.data.buf, inmsg.data.hdr.len));
                                    updateStats(0, 0, 0, 1, 0, 0, 0);
                                }
                                updateStats(0, 0, 1, 0, 0, 0, 0);
                            }
                            esito = 1;
                        }
                        else{
                            // ---- Utente non registrato ----
                            esito = -1;
                        }
                        PTHREAD_MUTEX_UNLOCK(&mux_users[lock_target2], "Errore mux POSTTXT_OP");

                        if (esito == -1)
                        {
                            // ---- Utente non registrato prima, mando OP_NICK_UNKNOWN ----
                            setHeader(&outmsg.hdr, OP_NICK_UNKNOWN, "");
                            PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");
                            SYS(sendHeader(fd, &outmsg.hdr), -1, "ERRORE senddata POSTTXT_OP");
                            PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");
                            updateStats(0, 0, 0, 0, 0, 0, 1);
                        }
                        else
                        {
                            // ---- Destinatario valido, mando OP_OK ----
                            setHeader(&outmsg.hdr, OP_OK, "");
                            PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");
                            SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader POSTTXT_OP");
                            PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "POSTTXT_OP mux");
                        }
                        my_free(outmsg.data.buf);
                    }
                } break;
                // ---- Opzione -p: RICHIESTA HISTORY DEI MESSAGGI ----
                case GETPREVMSGS_OP : {
                    printf("Worker[%d]: %s -> GETPREVMSGS_OP\n", idnumber, inmsg.hdr.sender);

                    user_data_t *node_data = NULL;
                    g_list *pvmsg_list = NULL;
                    hash1 = users->hash_function((void *) inmsg.hdr.sender) % users->nbuckets;

                    PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux getpvmsg");

                    // --- Prendo il puntatore alla history ----
                    if((node_data = icl_hash_find(users, inmsg.hdr.sender)) == NULL)
                        printf("Worker[%d]: ERRORE GETPREVMSGS_OP\n", idnumber);

                    pvmsg_list = node_data->pvmsg;
                    size_t nummsg = (size_t) pvmsg_list->num;

                    printf("Worker[%d]: Numero di messaggi precedenti: %d\n", idnumber, pvmsg_list->num);
                    setHeader(&(outmsg.hdr), OP_OK, "");
                    setData(&(outmsg.data), "", (char *)&nummsg, sizeof(size_t));
                    SYS(sendRequest(fd, &(outmsg)), -1, "Errore sendRequest GETPREVMSGS_OP");
                    if (nummsg > 0)
                    {
                        icl_entry_t * auxa = NULL;
                        icl_entry_t * corr = pvmsg_list->head;
                        while(corr!= NULL)
                        {
                            printf("Worker[%d]: Messaggio dalla history: %s\n", idnumber, ((msg_data_t *)corr->data)->buf);
                            setHeader(&outmsg.hdr, ((msg_data_t *)corr->data)->type, (char *)corr->key);
                            setData(&outmsg.data, inmsg.hdr.sender, ((msg_data_t *)corr->data)->buf, ((msg_data_t *)corr->data)->len);
                            SYS(sendRequest(fd, &outmsg), -1, "Errore sendRequest GETPREVMSGS_OP");
                            if(((msg_data_t *)corr->data)->type == TXT_MESSAGE)
                                updateStats(0, 0, 1, -1, 0, 0, 0);
                            // auxa = corr;
                            // free_data_msg(auxa->data);
                            // free_pointer(auxa->key);
                            // my_free(auxa);
                            corr = corr->next;
                        }
                        // my_free(pvmsg_list);
                        destroy_g(pvmsg_list);
                    }
                    node_data->pvmsg = new_glist(ops.MaxHistMsgs, string_compare, free_data_msg, free_pointer);
                    PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux getpvmsg");
                } break;
                // ---- Opzione -S: INVIO MESSAGGIO A TUTTI ----
                case POSTTXTALL_OP: {
                    printf("Worker[%d]: %s -> POSTTXTALL_OP\n", idnumber, inmsg.hdr.sender);
                    unsigned int hash1 = users->hash_function((void *) inmsg.hdr.sender) % users->nbuckets;

                    if (strcmp(inmsg.hdr.sender, inmsg.data.hdr.receiver) == 0)
                    {
                        setHeader(&outmsg.hdr, OP_FAIL, "");
                        PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux POSTTXTALL_OP");
                        SYS(sendHeader(fd, &outmsg.hdr), -1, "ERRORE sendheader POSTTXTALL_OP");
                        PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux POSTTXTALL_OP");
                        updateStats(0, 0, 0, 0, 0, 0, 1);
                    }
                    // ---- Controllo che il messaggio non ecceda la dimensione massima ----
                    else if (inmsg.data.hdr.len > ops.MaxMsgSize)
                    {
                        setHeader(&outmsg.hdr, OP_MSG_TOOLONG, "");
                        PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux POSTTXTALL_OP");
                        SYS(sendHeader(fd, &outmsg.hdr), -1, "ERRORE sendheader POSTTXTALL_OP");
                        PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux POSTTXTALL_OP");
                        updateStats(0, 0, 0, 0, 0, 0, 1);
                    }
                    else
                    {
                        // ---- Messaggio valido ----
                        post_all(users, mux_users, inmsg.hdr.sender, new_msg_data(TXT_MESSAGE, inmsg.data.buf, inmsg.data.hdr.len));
                        setHeader(&outmsg.hdr, OP_OK, "");
                        PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux POSTTXTALL_OP");
                        SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader POSTTXTALL_OP");
                        PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux POSTTXTALL_OP");
                    }
                    my_free(inmsg.data.buf);
                } break;
                // ---- Opzione -C: DEREGISTRAZIONE ----
                case UNREGISTER_OP : {
                    printf("Worker[%d]: %s -> UNREGISTER_OP\n", idnumber, inmsg.hdr.sender);

                    hash1 = users->hash_function(inmsg.hdr.sender) % users->nbuckets;
                    int esito;

                    PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "unreg mux");
                    esito = icl_hash_delete(users, inmsg.hdr.sender, free_pointer, free_data_usr);
                    if (esito == -1)
                    {
                        setHeader(&outmsg.hdr, OP_NICK_UNKNOWN, "");
                        SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader UNREGISTER_OP");
                    }
                    else
                    {
                        setHeader(&outmsg.hdr, OP_OK, "");
                        SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader UNREGISTER_OP");
                    }
                    PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "unreg mux");
                    updateStats(-1, 0, 0, 0, 0, 0, 0);
                } break;
                // ---- Opzione -s: INVIO FILE ----
                case POSTFILE_OP : {
                    printf("Worker[%d]: %s -> POSTFILE_OP\n", idnumber, inmsg.hdr.sender);
                    printf("Worker[%d]: [%s]: %s -> %s\n", idnumber, inmsg.hdr.sender, inmsg.data.buf, inmsg.data.hdr.receiver);

                    message_t inmsg2;
                    setHeader(&inmsg2.hdr, OP_FAIL, "");
                    setData(&inmsg2.data, "", NULL, 0);

                    unsigned int hash1 = users->hash_function((void *)inmsg.hdr.sender) % users->nbuckets;

                    if (strcmp(inmsg.hdr.sender, inmsg.data.hdr.receiver) == 0)
                    {
                        printf("Worker[%d]: Errore, destinatario uguale a mittente\n", idnumber);
                        setHeader(&outmsg.hdr, OP_FAIL, "");
                        PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux POSTFILE_OP");
                        SYS(sendHeader(fd, &outmsg.hdr), -1, "ERROR sendheader POSTFILE_OP");
                        PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux POSTFILE_OP");
                        updateStats(0, 0, 0, 0, 0, 0, 1);
                    }
                    else
                    {
                        SYS(readData(fd, &(inmsg2.data)), -1, "Error readData POSTFILE_OP");
                        inmsg2.data.buf = realloc(inmsg2.data.buf, sizeof(char) * (inmsg2.data.hdr.len + 1));
                        inmsg2.data.buf[inmsg2.data.hdr.len] = '\0';
                        printf("Worker[%d]: Dimensione file %d lunghezza %d\n", idnumber, inmsg2.data.hdr.len, (int)strlen(inmsg2.data.buf));
                        printf("ops.maxfilesize %d\n", ops.MaxFileSize);
                        int sendfd = 0;

                        if (inmsg2.data.hdr.len > ops.MaxFileSize)
                        {
                            printf("Worker[%d]: Errore MsgTooLong, dimensione %d (max %d)\n", idnumber, inmsg2.data.hdr.len, ops.MaxFileSize);
                            setHeader(&outmsg.hdr, OP_MSG_TOOLONG, "");
                            PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux POSTFILE_OP");
                            SYS(sendHeader(fd, &outmsg.hdr), -1, "Error sendheader POSTFILE_OP");
                            PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux POSTFILE_OP");
                            updateStats(0, 0, 0, 0, 0, 0, 1);
                        }
                        else
                        {
                            int hash2 = users->hash_function((void *)inmsg.data.hdr.receiver) % users->nbuckets;
                            int i = 0;
                            int guardia = 0;
                            char file_path[MAX_PATH_LENGTH];
                            memset(file_path, 0, sizeof(char) * MAX_PATH_LENGTH);

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
                            printf("PATHTH %s\n", file_path);

                            FILE *new_file;
                            user_data_t *ax = NULL;

                            setHeader(&outmsg.hdr, FILE_MESSAGE, inmsg.hdr.sender);
                            setData(&outmsg.data, inmsg.data.hdr.receiver, file_path, strlen(file_path) + 1);

                            PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash2)], "mux postfile");
                            if ((ax = (user_data_t *) icl_hash_find(users, (void *)inmsg.data.hdr.receiver)) == NULL)
                            {
                                PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash2)], "mux postfile");
                                printf("Worker[%d]: Utente non registrato\n", idnumber);
                                setHeader(&outmsg.hdr, OP_NICK_UNKNOWN, "");
                                PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux POSTFILE_OP");
                                SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader POSTFILE_OP");
                                PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux POSTFILE_OP");
                                updateStats(0, 0, 0, 0, 0, 0, 1);
                            }
                            else
                            {
                                // ---- Utente registrato ----

                                SYS(new_file = fopen(file_path, "w+"), NULL, "fopen");
                                int i = 0;
                                while (i < inmsg2.data.hdr.len)
                                {
                                    fputc(inmsg2.data.buf[i], new_file);
                                    i++;
                                }
                                fclose(new_file);
                                if (ax->fd == -1)
                                {
                                    printf("Worker[%d]: ---- UTENTE OFFLINE ----\n", idnumber);
                                    add_g(ax->pvmsg, new_string(inmsg.hdr.sender), new_msg_data(FILE_MESSAGE, file_path, strlen(file_path) + 1));
                                    updateStats(0, 0, 0, 0, 0, 1, 0);
                                }
                                else
                                {
                                    sendfd = ax->fd;
                                    // ---- Utente in linea, mando il messaggio ----
                                    printf("Worker[%d]: ---- UTENTE IN LINEA ----\n", idnumber);
                                    SYS(sendRequest(sendfd, &outmsg), -1, "Errore sendRequest POSTFILE");
                                    if(errno == EPIPE){
                                      printf("Worker[%d]: ---- UTENTE OFFLINE ----\n", idnumber);
                                      add_g(ax->pvmsg, new_string(inmsg.hdr.sender), new_msg_data(FILE_MESSAGE, file_path, strlen(file_path) + 1));
                                      updateStats(0, 0, 0, 0, 0, 1, 0);
                                    }
                                    updateStats(0, 0, 0, 0, 1, 0, 0);
                                }
                                PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash2)], "mux postfile");

                                PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux POSTFILE_OP");
                                setHeader(&outmsg.hdr, OP_OK, "");
                                SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader POSTFILE");
                                PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux POSTFILE_OP");
                            }
                        }
                        my_free(inmsg2.data.buf);
                    }
                    my_free(inmsg.data.buf);
                } break;
                // ---- Opzione: RICHIESTA FILE
                case GETFILE_OP : {
                    printf("Worker[%d]: %s -> GETFILE_OP\n", idnumber, inmsg.hdr.sender);
                    printf("Worker[%d]: File richiesto: %s\n", idnumber, inmsg.data.buf);
                    FILE * file_d;

                    unsigned int hash1 = users->hash_function((void *)inmsg.hdr.sender) % users->nbuckets;

                    if((file_d = fopen(inmsg.data.buf, "r")) == NULL){
                        printf("Worker[%d]: Errore file non trovato\n", idnumber);
                        setHeader(&outmsg.hdr, OP_NO_SUCH_FILE, "");
                        PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux GETFILE_OP");
                        SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader GETFILE");
                        PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux GETFILE_OP");
                        updateStats(0, 0, 0, 0, 0, 0, 1);
                    }
                    else
                    {
                        fseek(file_d, 0L, SEEK_END);
                        size_t size = ftell(file_d);
                        fseek(file_d, 0L, SEEK_SET);
                        char * file_content = (char *) my_malloc(size);
                        size_t i;
                        int k;
                        for(i=0, k=0; i<size; i++, k++){
                            file_content[k] = fgetc(file_d);
                        }

                        printf("Worker[%d]: File trovato, dimensione file: %d\n", idnumber, (int) size);

                        setHeader(&outmsg.hdr, OP_OK, "");
                        setData(&outmsg.data, "", file_content, (unsigned int)size);

                        PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux GETFILE_OP");
                        SYS(sendHeader(fd, &outmsg.hdr), -1, "Errore sendHeader GETFILE");
                        SYS(sendData(fd, &outmsg.data), -1, "Errore sendData GETFILE");
                        PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux GETFILE_OP");

                        updateStats(0, 0, 0, 0, 1, -1, 0);
                        SYS(fclose(file_d), -1, "fclose");
                        my_free(file_content);
                    }
                    my_free(inmsg.data.buf);
                } break;
                default : {
                    unsigned int hash1 = users->hash_function((void *)inmsg.hdr.sender) % users->nbuckets;
                    printf("Da implementare!\n");
                    setHeader(&outmsg.hdr, OP_FAIL, "");
                    PTHREAD_MUTEX_LOCK(&mux_users[hash_lock(hash1)], "mux DEFAULT_OP");
                    SYS(sendHeader(fd, &outmsg.hdr), -1, "sendHeader DEFAULT");
                    PTHREAD_MUTEX_UNLOCK(&mux_users[hash_lock(hash1)], "mux DEFAULT_OP");
                } break;
                }

                // ---- Rimetto il fd nel set ---- //
                PTHREAD_MUTEX_LOCK(&mux_fd, "mux fd");
                printf("Worker[%d]: Rimetto il fd %d nel set\n", idnumber, fd);
                FD_SET(fd, &set);
                if(fd > fd_num)
                    fd_num = fd;
                PTHREAD_MUTEX_UNLOCK(&mux_fd, "mux fd");
            }
            else {
                // ---- Il client ha finito le richieste, il suo fd ---- //
                // ---- viene chiuso e non viene riaggiunto al set  ---- //
                // ---- DISCONNECT_OP ---- ///
                printf("Worker[%d]: DISCONNECT_OP\n", idnumber);
                printf("Worker[%d]: Immagine della hashmap\n", idnumber);
                char * bx = set_offline(users, mux_users, fd);

                updateStats(0, -1, 0, 0, 0, 0, 0);
                if(bx == NULL)
                {
                    printf("Worker[%d]: Deregistrazione utente\n", idnumber);
                }
                else
                {
                    printf("Worker[%d]: EOF File descriptor di %s (%d) chiuso.\n", idnumber, bx, fd);
                    free_pointer(bx);
                }
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
pthread_t * createPool(int numthreads, void * (fun)(void*), int ** arg){
    pthread_t * pool = (pthread_t *) my_malloc(sizeof(pthread_t)*numthreads);
    int i;
    int * thread_numbers = (int*) my_malloc(sizeof(int) * numthreads);
    for(i=0; i<numthreads; i++){
        thread_numbers[i] = i+1;
        if(pthread_create(&pool[i], NULL, fun, &thread_numbers[i]) == -1){
            fprintf(stderr, "Master: CreatePool pthread_create\n");
            return NULL;
        }
    }
    *arg = thread_numbers;
    return pool;
}
int main(int argc, char *argv[]) {

    int fd_skt, fd_c;
    int i, aux;
    int desc_rdy = 0;
    struct timeval timeout;
    if(argc < 2){
        usage("./chatty");
        return -1;
    }

    // ---- Estraggo le opzioni dal file di configurazione ----
    if(parser(argv[2]) == -1){
        fprintf(stderr, "Master: errore nel parsing\n");
        printf("Master: Using default options\n");
    }
    printOptions();
    fflush(stdout);
    /* maschero tutti i segnali finchè i gestori permanenti non sono istallati */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGPIPE);
    if(pthread_sigmask(SIG_BLOCK, &mask, &oldmask)){
        perror("pthread_sigmask");
        return -1;
    }
    printf("ops.StatfileName %s\n", ops.StatFileName);
    SYS(stat_file = fopen(ops.StatFileName, "w"), NULL, "fopen stat_file");

    int * hand_thread_id;
    pthread_t * hand = createPool(1, handler_worker, &hand_thread_id);

    // ---- Inizializzo le liste ---- //
    pthread_mutex_init(&mux_task_queue, NULL);
    pthread_cond_init(&cond_taskq, NULL);
    pthread_mutex_init(&mux_fd, NULL);
    pthread_mutex_init(&mux_conn_map, NULL);
    task_queue = new_glist(int_compare, NULL, free_pointer);
    users = icl_hash_create(HASH_TABLE_BUCKETS, NULL, NULL);
    //conn_map = icl_hash_create(ops.MaxConnections / 2, NULL, int_compare);
    mux_users = (pthread_mutex_t*) my_malloc(sizeof(pthread_mutex_t)*HASH_TABLE_BUCKETS);

    for(i=0; i<HASH_TABLE_NMUTEX; i++)
        pthread_mutex_init(&mux_users[i], NULL);

    // ---- Creo il pool di thread ---- //
    int * thread_ids;
    pthread_t * pool = createPool(ops.ThreadsInPool, worker, &thread_ids);

    // ---- Creo il socket ---- //
    SYS(fd_skt = socket(AF_UNIX, SOCK_STREAM, 0), -1, "socket");

    // ---- Inizializzo l'indirizzo dove il server sarà in ascolto ---- //
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
    while(!eflag){
        // ---- Copio il set aggiornato in quello che verrà passato alla select ---- //
        pthread_mutex_lock(&mux_fd);
        memcpy(&rdset, &set, sizeof(set));
        pthread_mutex_unlock(&mux_fd);

        // ---- Setto il timer della select a 0 ---- //
        timeout.tv_sec = 0;
        timeout.tv_usec = 5000;
        // ---- Poichè il timer è a 0, la select controllerà immediatamente il contenuto di rdset ---- //
        // ---- Se ci sono file descriptor pronti li gestisco, altrimenti la select viene rieseguita ----  //
        if((desc_rdy = select(fd_num+1, &rdset, NULL, NULL, &timeout)) == -1){
            perror("select");
            fprintf(stderr, "ERROR: facendo la select\n");
            //return -1;
        }
        else if(desc_rdy != 0){
            // ---- desc_rdy file descriptors sono pronti ---- //

            // ---- Scorro tutti i file descriptor ---- //
            for(i=0; i<=fd_num && desc_rdy > 0; i++){
                if(FD_ISSET(i, &rdset)){
                    desc_rdy--;
                    printf("Master: %d is set\n", i);

                    // ---- Se il descrittore pronto è il socket faccio l'accept e lo metto  ---- //
                    // ---- nel set, altrimenti passo il fd ai workers e lo tolgo dal set    ---- //
                    if(i == fd_skt){
                        SYS(fd_c = accept(fd_skt, NULL, 0), -1, "accept");
                        pthread_mutex_lock(&mux_fd);
                        FD_SET(fd_c, &set);
                        if(fd_c > fd_num)
                            fd_num = fd_c;
                        pthread_mutex_unlock(&mux_fd);
                    }
                    else{
                      // ----
                      pthread_mutex_lock(&mux_fd);
                      printf("Master: Tolgo il fd %d dal set\n", i);
                      FD_CLR(i, &set);
                      if (i == fd_num)
                      {
                        while (FD_ISSET(fd_num, &set) == 0)
                        fd_num--;
                      }
                      pthread_mutex_unlock(&mux_fd);

                      PTHREAD_MUTEX_LOCK(&mux_task_queue, "lock task_queue");
                      printf("Master: Metto il fd %d in coda\n", i);
                      tot_task++;
                      add_g(task_queue, new_integer(i), NULL);
                      pthread_cond_signal(&cond_taskq);
                      PTHREAD_MUTEX_UNLOCK(&mux_task_queue, "lock task_queue");
                    }
                }
                else{

                }
            }
        }
    }
    printf("\nTerminating program:\n");
    printf("TOT_TASK %d\n", tot_task);
    printf("Waiting for the destruction of the threads..\n");
    void ** retval = (void**) my_malloc(sizeof(void*)*ops.ThreadsInPool);
    for(i=0; i<ops.ThreadsInPool; i++){
        pthread_join(pool[i], &retval[i]);
        printf("Worker[%d] returns.\n", *(int*) retval[i]);
    }
    my_free(thread_ids);
    my_free(retval);
    printf("Closing the stat file..\n");
    SYS(fclose(stat_file), -1, "fclose stat_file");
    // printf("Deleting stat file...\n");
    // SYS(unlink(ops.StatFileName), -1, "unlink StatFile");
    printf("Freeing thread array..\n");
    my_free(pool);
    printf("Freeing mutex array..\n");
    my_free(mux_users);
    printf("HT image:\n");
    icl_hash_dump(stdout, users);
    printf("Destroying hash table..\n");
    icl_hash_destroy(users, free_pointer, free_data_usr);
    printf("Destroying file descriptors queue..\n");
    icl_hash_destroy(conn_map, free_pointer, free_pointer);
    printf("Deleting socket..\n");
    SYS(unlink(ops.UnixPath), -1, "unlink UnixPath");
    destroy_g(task_queue);
    printf("Freeing thread handler..\n");
    pthread_join(hand[0], NULL);
    my_free(hand_thread_id);
    my_free(hand);
    return 0;
}
