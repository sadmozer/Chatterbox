/*
 * membox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */
/**
 * @file config.h
 * @brief File contenente alcune define con valori massimi utilizzabili
 */

#if !defined (CONFIG_H_)
#define CONFIG_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#define MAX_NAME_LENGTH 32
#define MAX_PATH_LENGTH 100
#define HASH_TABLE_BUCKETS 128
#define HASH_TABLE_NMUTEX 32
#define MAX_HIST_LENGHT 64
#define MAX_FILE_KB 8192
#define MAX_CONNECTION_NUMBER 64
#define MAX_THREADPOOL_NUMBER 32
#define MAX_MSG_SIZE_NUMBER 2048

/* aggiungere altre define qui */
#define DF_UNIXPATH "/tmp/chatty_socket_534015"
#define DF_MAXCONNECTIONS 16
#define DF_THREADSINPOOL 5
#define DF_MAXMSGSIZE 512
#define DF_MAXFILESIZE 1024
#define DF_MAXHISTMSGS 16
#define DF_DIRNAME "/tmp/chatty_534015"
#define DF_STATFILENAME "/tmp/chatty_stats_534015.txt"

#define PTHREAD_MUTEX_UNLOCK(X, Y) if(pthread_mutex_unlock(X) != 0){ perror(Y);}

#define SYS(x, y, z) if((x)==y){ perror(z);}

// to avoid warnings like "ISO C forbids an empty translation unit"
typedef int make_iso_compilers_happy;

static inline void PTHREAD_MUTEX_LOCK(pthread_mutex_t * X, const char * Y) {
  //printf("Mi blocco qui?\n");
  if(pthread_mutex_lock(X) != 0)
    perror(Y);
  //printf("NO!\n");
}
static inline void * my_malloc(size_t size){
    void * p = (void *) malloc(size);
    if (!p) {
	    perror("malloc");
	    fprintf(stderr, "ERRORE: Out of memory\n");
	    exit(EXIT_FAILURE);
    }
    // printf("malloc %p %d\n", (void*) p, (int) size);
    //fflush(stdout);
    return p;
}
static inline void my_free(void * p){
    if(p != NULL)
        free(p);
    // printf("free %p\n", (void*) p);
    //fflush(stdout);
}



#endif /* CONFIG_H_ */
