/*
 * chatterbox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */

/**
 * @file connection.h
 * @author Niccolò Cardelli 534015
 * @copyright 2018 Niccolò Cardelli 534015
 * @brief Contiene le dichiarazioni delle funzioni che implementano il protocollo
 *        tra i clients ed il server
 */

#if !defined (CONNECTIONS_H_)
#define CONNECTIONS_H_

#include "message.h"


int openConnection(char *path, unsigned int ntimes, unsigned int secs);

int readHeader(long connfd, message_hdr_t *hdr);
int readData(long fd, message_data_t *data);
int readMsg(long fd, message_t *msg);

int sendRequest(long fd, message_t *msg);
int sendData(long fd, message_data_t *msg);
int sendHeader(long connfd, message_hdr_t *hdr);

#endif /* CONNECTIONS_H_ */
