/*
 * membox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */

/**
 * @file config.h
 * @author Niccolò Cardelli 534015
 * @copyright 2018 Niccolò Cardelli 534015
 * @brief File contenente alcune define con valori massimi utilizzabili e
 *        alcune funzioni di utilita'
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

#if !defined (CONFIG_H_)
#define CONFIG_H_


#define MAX_NAME_LENGTH 32
#define MAX_PATH_LENGTH 100
#define HASH_TABLE_BUCKETS 128
#define HASH_TABLE_NMUTEX 32
#define MAX_HIST_LENGHT 64
#define MAX_FILE_KB 8192
#define MAX_CONNECTION_NUMBER 64
#define MAX_THREADPOOL_NUMBER 32
#define MAX_MSG_SIZE_NUMBER 2048
#define HASH_TABLE_GROUPS 32

#define DF_UNIXPATH "/tmp/chatty_socket_534015"
#define DF_MAXCONNECTIONS 16
#define DF_THREADSINPOOL 5
#define DF_MAXMSGSIZE 512
#define DF_MAXFILESIZE 1024
#define DF_MAXHISTMSGS 16
#define DF_DIRNAME "/tmp/chatty_534015"
#define DF_STATFILENAME "/tmp/chatty_stats_534015.txt"

#define SYS(x, y, z) if((x)==y){ perror(z); errno = 0; }

// to avoid warnings like "ISO C forbids an empty translation unit"
typedef int make_iso_compilers_happy;

/**
 * @brief Wrapper di perror
 * @param  err -- stringa di errore
 */
static inline void PERROR(const char *err)
{
  perror(err);
  errno = 0;
}

/**
 * @brief Wrapper di pthread_mutex_lock
 * @param mutex -- lock
 * @param  err -- stringa di errore
 */
static inline void PTHREAD_MUTEX_LOCK(pthread_mutex_t *mutex, const char *err)
{
  if(pthread_mutex_lock(mutex) != 0) {
    perror(err);
    errno = 0;
  }
}

/**
 * @brief Wrapper di pthread_mutex_unlock
 * @param mutex -- lock
 * @param  err -- stringa di errore
 */
static inline void PTHREAD_MUTEX_UNLOCK (pthread_mutex_t *mutex, const char *err)
{
  if(pthread_mutex_unlock(mutex) != 0) {
    perror(err);
    errno = 0;
  }
}

/**
 * @brief Wrapper di malloc
 * @param size -- numero di byte da allocare
 */
static inline void * my_malloc(size_t size)
{
    void *p = (void *) malloc(size);
    if (!p) {
	    perror("malloc");
	    pthread_exit((void*) -1);
    }
    return p;
}

/**
 * @brief Wrapper di free
 * @param p -- puntatore alla memoria da deallocare
 */
static inline void my_free(void *p)
{
    if(p != NULL)
      free(p);
}

#endif /* CONFIG_H_ */
