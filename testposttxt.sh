#!/bin/bash

#
# chatterbox Progetto del corso di LSO 2017
#
# Dipartimento di Informatica Università di Pisa
# Docenti: Prencipe, Torquati
#

# @file testposttxt.sh
# @author Niccolò Cardelli 534015
# @copyright 2018 Niccolò Cardelli 534015
# @brief Contiene un semplice test

/usr/bin/valgrind --leak-check=full ./chatty -f DATA/chatty.conf1 >& ./valgrind_out &
pidchatty=$!

echo $pidchatty

sleep 2

./client -l /tmp/chatty_socket -c pippo
./client -l /tmp/chatty_socket -c topolino

./client -l /tmp/chatty_socket -k pippo -R -1 &
clientpid=$!

sleep 1

for((i=0; i<150; i++)); do
  ./client -l /tmp/chatty_socket -k topolino -S ciao:pippo &
done

sleep 4

PS=$(ps | grep client | grep -c ^)

if [[ $PS != 1 ]]; then
  echo "Test Fallito"
  exit 1
fi

echo "Invio SIGUSR1.."
kill -10 $pidchatty
sleep 1

ONLINE=$(tail -13 /tmp/chatty_stats.txt | cut -d " " -f 4)

if [[ $ONLINE != 1 ]]; then
  echo "Test Fallito"
  exit 1
fi

kill -15 $clientpid
sleep 1

kill -15 $pidchatty
sleep 2

echo "Test Superato!"
