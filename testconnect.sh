#!/bin/bash

#
# chatterbox Progetto del corso di LSO 2017
#
# Dipartimento di Informatica Università di Pisa
# Docenti: Prencipe, Torquati
#

# @file testconnect.sh
# @author Niccolò Cardelli 534015
# @copyright 2018 Niccolò Cardelli 534015
# @brief Contiene un semplice test

/usr/bin/valgrind --leak-check=full ./chatty -f DATA/chatty.conf1 >& ./valgrind_out &
pidchatty=$!

echo $pidchatty

sleep 3

for((i=0; i<50; i++)); do
  ./client -l /tmp/chatty_socket -c "utente$i"
done

sleep 2

for((i=0; i<50; i++)); do
  ./client -l /tmp/chatty_socket -k "utente$i" -R 1 &
done

sleep 2

CLIENTS=$(ps | grep client | grep -c ^)

if [[ $CLIENTS != 32 ]]; then
  echo "Test Fallito"
  exit 1
fi

TMP=$(ps | grep client | cut -d' ' -f 1 | tr '\n' ' ')

kill -15 $TMP

sleep 1

kill -15 $pidchatty

sleep 2

echo "Test Superato!"
