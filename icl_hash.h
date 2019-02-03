/*
 * membox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */

/**
 * @file icl_hash.h
 *
 * @brief Header file for icl_hash routines.
 *
 */
/* $UTK_Copyright: $ */

#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <string.h>

#include "config.h"

#ifndef icl_hash_h
#define icl_hash_h

#if defined(c_plusplus) || defined(__cplusplus)
extern "C" {
#endif

/**
 * @struct icl_entry_t
 * @brief nodo generico
 * @var key -- puntatore alla chiave univoca
 * @var data -- puntatore alla parte dati
 * @var next -- puntatore al nodo successivo
 */
typedef struct icl_entry_s {
    void *key;
    void *data;
    struct icl_entry_s *next;
} icl_entry_t;

/** @struct icl_hash_t
 * @brief tabella hash
 * @var nbuckets -- numero di liste linkate che compongono la tabella
 * @var nentries -- numero totale di elementi
 * @var buckets -- array dei puntatori alle liste linkate
 * @var hash_function -- funzione di hashing
 * @var hash_key_compare -- funzione di confronto
 */
typedef struct icl_hash_s {
    int nbuckets;
    int nentries;
    icl_entry_t **buckets;
    unsigned int (*hash_function)(void*);
    int (*hash_key_compare)(void*, void*);
} icl_hash_t;

/* ---- Funzioni ---- */
icl_hash_t * icl_hash_create( int nbuckets, unsigned int (*hash_function)(void*), int (*hash_key_compare)(void*, void*) );

int icl_hash_destroy(icl_hash_t *, void (*)(void*), void (*)(void*));

icl_entry_t * icl_hash_find(icl_hash_t *, void*);
icl_entry_t * icl_hash_insert(icl_hash_t *, void*, void *);
icl_entry_t * icl_hash_update_insert(icl_hash_t *, void*, void *, void **);
int icl_hash_delete( icl_hash_t *ht, void* key, void (*free_key)(void*), void (*free_data)(void*) );
int icl_hash_dump(FILE *, icl_hash_t *);

void icl_hash_apply_until(icl_hash_t *tab, pthread_mutex_t *mux, int vfun(icl_entry_t *corr, void **args), void **argv);

#if defined(c_plusplus) || defined(__cplusplus)
}
#endif


/**
 * @brief Restituisce l'indice della lock corrispondente ad un bucket
 * @param hash -- indice del bucket
 * @return indice della lock
 */
static inline unsigned int hash_lock(unsigned int hash)
{
  return hash / (HASH_TABLE_BUCKETS / HASH_TABLE_NMUTEX);
}

/* ---- Funzioni di confronto ---- */

/**
 * @brief Confronta due stringhe
 *
 * @param a -- prima stringa
 * @param b -- seconda stringa
 *
 * @return  1 se sono uguali,
 *          0 altrimenti
 */
static inline int string_compare(void* a, void* b)
{
    return (strcmp( (char*)a, (char*)b ) == 0);
}

/**
 * @brief Confronta due interi
 *
 * @param a --  primo intero
 * @param b -- secondo intero
 *
 * @return 1 se sono uguali,
 *         0 altrimenti
 */
static inline int int_compare(void * a, void * b){
    return *(int*)(a) == *(int*)(b);
}

#endif /* icl_hash_h */
