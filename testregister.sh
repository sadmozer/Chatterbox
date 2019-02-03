#!/bin/bash

#
# chatterbox Progetto del corso di LSO 2017
#
# Dipartimento di Informatica Università di Pisa
# Docenti: Prencipe, Torquati
#

# @file testregister.sh
# @author Niccolò Cardelli 534015
# @copyright 2018 Niccolò Cardelli 534015
# @brief Contiene un semplice test

/usr/bin/valgrind --leak-check=full ./chatty -f DATA/chatty.conf1 >& ./valgrind_out &
pidchatty=$!

echo $pidchatty

sleep 3

for((i=0; i<50; i++)); do
  ./client -l /tmp/chatty_socket -c utente$i
done

for((i=0; i<49; i++)); do
  ./client -l /tmp/chatty_socket -k utente$i -C utente$i
done

kill -10 $pidchatty

sleep 1

kill -3 $pidchatty

sleep 2

TMP=$(grep "icl_hash_dump:" ./valgrind_out | grep -c ^)

if [[ $TMP != 1 ]]; then
  echo "Test Fallito"
  exit 1
fi

echo "Test Superato!"
