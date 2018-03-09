/*
 * chatterbox Progetto del corso di LSO 2017
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 *
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
 * @file  connection.h
 * @brief Contiene le funzioni che implementano il protocollo
 *        tra i clients ed il server
 */
/**
 * @function openConnection
 * @brief Apre una connessione AF_UNIX verso il server
 *
 * @param path Path del socket AF_UNIX
 * @param ntimes numero massimo di tentativi di retry
 * @param secs tempo di attesa tra due retry consecutive
 *
 * @return il descrittore associato alla connessione in caso di successo
 *         -1 in caso di errore
 */
int openConnection(char *path, unsigned int ntimes, unsigned int secs) {
    int fd_skt = -1;
    int aux = -1;

    if ((fd_skt = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
        perror("socket");

    struct sockaddr_un add;
    memset(&add, '0', sizeof(add));
    add.sun_family = AF_UNIX;
    strncpy(add.sun_path, path, strlen(path) + 1);

    while (aux == -1)
    {
        aux = connect(fd_skt, (struct sockaddr *)&add, sizeof(add));
        if (aux == -1)
            sleep(MAX_SLEEPING);
    }
    return fd_skt;
}

/**
 * @function readHeader
 * @brief Legge l'header del messaggio
 *
 * @param fd     descrittore della connessione
 * @param hdr    puntatore all'header del messaggio da ricevere
 *
 * @return <=0 se c'e' stato un errore
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa)
 */
int readHeader(long connfd, message_hdr_t *hdr){
    int read1;
    //printf("readHeader: totale byte da ricevere %d\n", (int)sizeof(*hdr));
    memset(hdr, 0, sizeof(message_hdr_t));
    read1 = read(connfd, hdr, sizeof(message_hdr_t));
    if(read1 == -1){
        perror("readHeader");
        return -1;
    }
    if(read1 == 0){
        printf("readHeader: EOF\n");
        return 0;
    }
    // printf("readHeader: totale byte ricevuti %d\n", (int)tot);
    return (int) read1;
}
/**
 * @function readData
 * @brief Legge il body del messaggio
 *
 * @param fd     descrittore della connessione
 * @param data   puntatore al body del messaggio
 *
 * @return <=0 se c'e' stato un errore
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa)
 */
int readData(long fd, message_data_t *data){
    int read1, read2 = 0;
    memset(data, 0, sizeof(message_data_hdr_t));
    //printf("readData: byte data.hdr da ricevere %d\n", (int) sizeof(data->hdr));
    read1 = read(fd, &(data->hdr), sizeof(message_data_hdr_t));
    if(read1 == -1){
        perror("readData");
        return -1;
    }
    else if(read1 == 0){
        printf("readData: EOF\n");
        return 0;
    }
    // printf("readData: byte data.hdr ricevuti %d\n", (int)tot);
    // printf("readData: byte data.buf da ricevere %d\n", (int)data->hdr.len);
    //printf("data->hdr.len usigned %d\n int %d\nsizeof(size_t) %d\n", (unsigned int) data->hdr.len, (int) data->hdr.len, (int) sizeof(size_t));
    if(data->hdr.len <= 0){
        data->buf = NULL;
        return read1;
    }
    else{
        data->buf = (char *) my_malloc(sizeof(char) * (data->hdr.len));
        memset(data->buf, 0, sizeof(char) * (data->hdr.len));
        while(data->hdr.len - read2 > 0){
            read2 += read(fd, data->buf + read2, sizeof(char) * data->hdr.len - read2);
            printf("readData: byte data.buf ricevuti %d\n", read2);
            if(read2 == -1) {
                perror("readData");
                return -1;
            }
            else if (read2 == 0){
                printf("readData: EOF\n");
                return 0;
            }
        }
        //printf("data->buf %s\n", data->buf);
    }
    // }
    //printf("readData: totale byte ricevuti %d\n", (int)tot);
    return (int) read2 + read1;
}

/**
 * @function readMsg
 * @brief Legge l'intero messaggio
 *
 * @param fd     descrittore della connessione
 * @param data   puntatore al messaggio
 *
 * @return <=0 se c'e' stato un errore
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa)
 */
int readMsg(long fd, message_t *msg){
    int read1, read2;
    memset(msg, 0, sizeof(message_t));
    //printf("readMsg: totale byte da ricevere %d\n", (int)sizeof(*msg));
    read1 = readHeader(fd, &(msg->hdr));
    if(read1 >= 0)
        read2 = readData(fd, &(msg->data));
    return (read1 != -1 && read2 != -1) ? read1 + read2: -1;
}
/* da completare da parte dello studente con altri metodi di interfaccia */

/**
 * @function sendRequest
 * @brief Invia un messaggio di richiesta al server
 *
 * @param fd     descrittore della connessione
 * @param msg    puntatore al messaggio da inviare
 *
 * @return <=0 se c'e' stato un errore
 */
int sendRequest(long fd, message_t *msg){
    int write1, write2;
    //printf("sendRequest: totale byte da inviare %d\n", (int)sizeof(*msg));
    write1 = sendHeader(fd, &msg->hdr);
    if(write1 >= 0)
        write2 = sendData(fd, &msg->data);
    return (write1 != -1 && write2 != -1) ? write1 + write2: -1;
}

/**
 * @function sendData
 * @brief Invia il body del messaggio al server
 *
 * @param fd     descrittore della connessione
 * @param msg    puntatore al messaggio da inviare
 *
 * @return <=0 se c'e' stato un errore
 */
int sendData(long fd, message_data_t *msg){
    int write1, write2 = 0;
    //printf("sendData: byte data.hdr da inviare %d\n", (int)sizeof(msg->hdr));
    //printf("msg->hdr.len %d\n", (unsigned int) msg->hdr.len);
    write1 = write(fd, &msg->hdr, sizeof(message_data_hdr_t));
    //printf("sendData1: byte scritti %d\n", write1);
    if(write1 == -1){
        perror("sendData1");
        return -1;
    }
    else if(write1 == 0){
        printf("sendData1: EOF\n");
        return 0;
    }

    //printf("sendData2: byte data.buf da inviare %d\n", msg->hdr.len);
    if(msg->hdr.len <= 0)
        return write1;
    else{
        while(msg->hdr.len - write2 > 0){
            write2 += write(fd, msg->buf + write2, sizeof(char) * msg->hdr.len - write2);
            printf("sendData2: byte scritti %d\n", write2);
            if(write2 == -1){
                perror("sendData2");
                return -1;
            }
            else if(write2 == 0){
                printf("sendData2: EOF\n");
                return 0;
            }
        }
    }
    //printf("sendData: totale byte inviati %d\n", (int) tot);
    return (int) write1 + write2;

}

/* da completare da parte dello studente con eventuali altri metodi di interfaccia */

int sendHeader(long connfd, message_hdr_t *hdr){
    int write1;
    //printf("sendHeader: totale byte da inviare %d\n", (int)sizeof(*hdr));
    write1 = write(connfd, hdr, sizeof(message_hdr_t));
    if(write1 == -1){
        perror("sendHeader");
        return -1;
    }
    else if(write1 == 0){
        printf("sendHeader: EOF\n");
        return 0;
    }
    //printf("sendHeader: totale byte inviati %d\n", (int)tot);
    return write1;
}
