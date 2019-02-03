#!/bin/bash

#
# chatterbox Progetto del corso di LSO 2017
#
# Dipartimento di Informatica Università di Pisa
# Docenti: Prencipe, Torquati
#

# @file testvalgrind.sh
# @author Niccolò Cardelli 534015
# @copyright 2018 Niccolò Cardelli 534015
# @brief Contiene uno script eseguire il server con valgrind

USE="use $0 config_file"

if [[ $# != 1 ]]; then
  echo $USE
  exit 1
elif [[ -d $1 ]]; then
  echo "errore: config_file e' una directory"
  echo $USE
  exit 1
elif ! [[ -f $1 ]]; then
  echo "errore: config_file non e' un file"
  echo $USE
  exit 1
elif ! [[ -e $1 ]]; then
  echo "errore: config_file e' un file speciale"
  echo $USE
  exit 1
elif ! [[ -r $1 ]]; then
  echo "errore: config_file permesso negato"
  echo $USE
  exit 1
fi

/usr/bin/valgrind --leak-check=full --track-origins=yes --show-leak-kinds=all ./chatty -f $1 >& ./valgrind_out &
pidchatty=$!

echo $pidchatty
