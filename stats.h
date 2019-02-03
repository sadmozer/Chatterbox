/*
 * chatterbox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */

 /**
 * @file stats.h
 * @brief Contiene la definizione della struttura delle statistiche e
 *        le relative funzioni di utilita'
 */

#if !defined(MEMBOX_STATS_)
#define MEMBOX_STATS_

#include <stdio.h>
#include <time.h>
#include <pthread.h>

#include "config.h"

struct statistics {
    unsigned long nusers;                       // n. di utenti registrati
    unsigned long nonline;                      // n. di utenti connessi
    unsigned long ndelivered;                   // n. di messaggi testuali consegnati
    unsigned long nnotdelivered;                // n. di messaggi testuali non ancora consegnati
    unsigned long nfiledelivered;               // n. di file consegnati
    unsigned long nfilenotdelivered;            // n. di file non ancora consegnati
    unsigned long nerrors;                      // n. di messaggi di errore
};

/**
 * @brief Stampa le statistiche nel file passato come argomento
 *
 * @param fout -- descrittore del file aperto in append.
 *
 * @return 0 in caso di successo, -1 in caso di fallimento
 */
static inline int printStats(FILE *fout) {
    extern struct statistics chattyStats;
    if (fprintf(fout, "%ld - %ld %ld %ld %ld %ld %ld %ld\n",
		(unsigned long)time(NULL),
		chattyStats.nusers,
		chattyStats.nonline,
		chattyStats.ndelivered,
		chattyStats.nnotdelivered,
		chattyStats.nfiledelivered,
		chattyStats.nfilenotdelivered,
		chattyStats.nerrors
		) < 0) return -1;
    fflush(fout);
    return 0;
}

/**
 * @brief Aggiorna la struttura che contiene i dati delle statistiche
 * @param nu --
 * @param no --
 * @param nd --
 * @param nnd --
 * @param nfd --
 * @param nfnd --
 * @param ne --
 *
 * @return <return_description>
 * @details <details>
 */
static inline void updateStats(unsigned long nu, unsigned long no, unsigned long nd, unsigned long nnd, unsigned long nfd, unsigned long nfnd, unsigned long ne) {
    extern struct statistics chattyStats;
    chattyStats.nusers += nu;
    chattyStats.nonline += no;
    chattyStats.ndelivered += nd;
    chattyStats.nnotdelivered += nnd;
    chattyStats.nfiledelivered += nfd;
    chattyStats.nfilenotdelivered += nfnd;
    chattyStats.nerrors += ne;
    printStats(stdout);
}
#endif /* MEMBOX_STATS_ */
