/*
 * chatterbox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
 */

/**
 * @file connections.c
 * @brief Contiene le funzioni che implementano il protocollo
 *        tra i clients ed il server
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "connections.h"
#include "config.h"


/**
 * @brief Apre una connessione AF_UNIX verso il server
 *
 * @param path -- Path del socket AF_UNIX
 * @param ntimes -- numero massimo di tentativi di retry
 * @param secs -- tempo di attesa tra due retry consecutive
 *
 * @return il descrittore associato alla connessione in caso di successo
 *         -1 in caso di errore
 */
int openConnection(char *path, unsigned int ntimes, unsigned int secs)
{
  int fd_skt = -1;
  int aux = -1;

  if ((fd_skt = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
    PERROR("socket");

  struct sockaddr_un add;
  memset(&add, '0', sizeof(add));
  add.sun_family = AF_UNIX;
  strncpy(add.sun_path, path, strlen(path) + 1);
  int i = ntimes;
  while (i > 0)
  {
    SYS(aux = connect(fd_skt, (struct sockaddr*) &add, sizeof(add)), -1, "connect");
    if(aux == -1) {
      sleep(secs);
      i--;
    }
    else
      i = 0;
  }
  return fd_skt;
}

/**
 * @brief Legge l'header del messaggio
 *
 * @param fd -- descrittore della connessione
 * @param hdr -- puntatore all'header del messaggio da ricevere
 *
 * @return numero di byte letti in caso di successo,
 *         0 in caso di connessione chiusa,
 *         -1 in caso di errore
 *
 */
int readHeader(long connfd, message_hdr_t *hdr)
{
  int read1;
  memset(hdr, 0, sizeof(message_hdr_t));
  read1 = read(connfd, hdr, sizeof(message_hdr_t));
  if(read1 == -1) {
    PERROR("readHeader");
    return -1;
  }
  if(read1 == 0) {
    printf("readHeader: EOF\n");
    return 0;
  }
  return (int) read1;
}

/**
 * @brief Legge il body del messaggio
 *
 * @param fd -- descrittore della connessione
 * @param data -- puntatore al body del messaggio
 *
 * @return numero di byte letti in caso di successo,
 *         0 in caso di connessione chiusa,
 *         -1 in caso di errore
 */
int readData(long fd, message_data_t *data)
{
  int read1, read2 = 0;
  memset(data, 0, sizeof(message_data_hdr_t));
  read1 = read(fd, &(data->hdr), sizeof(message_data_hdr_t));
  if(read1 == -1) {
    PERROR("readData");
    return -1;
  }
  else if(read1 == 0) {
    printf("readData: EOF\n");
    return 0;
  }
  if(data->hdr.len <= 0) {
    data->buf = NULL;
    return read1;
  }
  else {
    data->buf = (char*) my_malloc(sizeof(char) * (data->hdr.len));
    memset(data->buf, 0, sizeof(char) * (data->hdr.len));
    while(data->hdr.len - read2 > 0) {
      read2 += read(fd, data->buf + read2, sizeof(char) * data->hdr.len - read2);
      if(read2 == -1) {
        PERROR("readData");
        return -1;
      }
      else if (read2 == 0) {
        printf("readData: EOF\n");
        return 0;
      }
    }
  }
  return (int) read2 + read1;
}

/**
 * @brief Legge l'intero messaggio
 *
 * @param fd -- descrittore della connessione
 * @param data -- puntatore al messaggio
 *
 * @return numero di byte letti in caso di successo,
 *         0 in caso di connessione chiusa,
 *         -1 in caso di errore
 */
int readMsg(long fd, message_t *msg)
{
  int read1, read2;
  memset(msg, 0, sizeof(message_t));
  read1 = readHeader(fd, &(msg->hdr));
  if(read1 > 0) {
    read2 = readData(fd, &(msg->data));
    if(read2 > 0)
      return read1 + read2;
    else
      return read2;
  }
  else
    return read1;
}

/**
 * @brief Invia un messaggio di richiesta al server
 *
 * @param fd -- descrittore della connessione
 * @param msg -- puntatore al messaggio da inviare
 *
 * @return numero di byte scritti in caso di successo,
 *         0 in caso di connessione chiusa,
 *         -1 in caso di errore
 */
int sendRequest(long fd, message_t *msg)
{
  int write1, write2;
  //printf("sendRequest: totale byte da inviare %d\n", (int)sizeof(*msg));
  write1 = sendHeader(fd, &msg->hdr);
  if(write1 > 0) {
    write2 = sendData(fd, &msg->data);
    if(write2 > 0)
      return write2 + write1;
    else
      return write2;
  }
  else
    return write1;
}

/**
 * @function sendData
 * @brief Invia il body del messaggio al server
 *
 * @param fd -- descrittore della connessione
 * @param msg -- puntatore al messaggio da inviare
 *
 * @return numero di byte scritti in caso di successo,
 *         0 in caso di connessione chiusa,
 *         -1 in caso di errore
 */
int sendData(long fd, message_data_t *msg)
{
  int write1, write2 = 0;
  write1 = write(fd, &msg->hdr, sizeof(message_data_hdr_t));
  if(write1 == -1) {
    PERROR("sendData1");
    return -1;
  }
  else if(write1 == 0) {
    printf("sendData1: EOF\n");
    return 0;
  }

  if(msg->hdr.len <= 0)
    return write1;
  else {
    while(msg->hdr.len - write2 > 0) {
      write2 += write(fd, msg->buf + write2, sizeof(char) * msg->hdr.len - write2);
      if(write2 == -1) {
        PERROR("sendData2");
        return -1;
      }
      else if(write2 == 0) {
        printf("sendData2: EOF\n");
        return 0;
      }
    }
  }
  return (int) write1 + write2;
}

/**
 * @brief Invia l'header del messaggio al server
 *
 * @param fd -- descrittore della connessione
 * @param msg -- puntatore all'header del messaggio da inviare
 *
 * @return numero di byte scritti in caso di successo,
 *         0 in caso di connessione chiusa,
 *         -1 in caso di errore
 */
int sendHeader(long connfd, message_hdr_t *hdr)
{
  int write1;
  write1 = write(connfd, hdr, sizeof(message_hdr_t));
  if(write1 == -1) {
    PERROR("sendHeader");
    return -1;
  }
  else if(write1 == 0) {
    printf("sendHeader: EOF\n");
    return 0;
  }
  return write1;
}
