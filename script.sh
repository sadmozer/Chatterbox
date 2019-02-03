#!/bin/bash

#
# chatterbox Progetto del corso di LSO 2017
#
# Dipartimento di Informatica Università di Pisa
# Docenti: Prencipe, Torquati
#

# @file script.sh
# @author Niccolò Cardelli 534015
# @copyright 2018 Niccolò Cardelli 534015
# @brief Contiene lo script bash richiesto

USE=$(echo "usa $0 config_file minutes [-help]")

if [[ $# != 2 ]] && [[ $# != 3 ]]; then
  echo $USE
  exit 1
fi

FLAG=0
for ARG in $@ ; do
    case "$ARG" in
        -help) echo $USE
              exit 0
        ;;
    esac
done

echo "Config_file: $1"
echo "Minutes: $2"

if [[ -d $1 ]]; then
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

case $2 in ''|*[!0-9]*)
  echo "errore: $2 argomento non valido"
  echo $USE
  exit 1
  ;;
esac

PHASE0=$(grep -v "#" $1 | grep "DirName" | cut -d "=" -f 2)

echo "Files in $1 più vecchi di $2 minuto/i: "
PHASE1=$(find $PHASE0 -type f -cmin +$2)

\rm -fr  $PHASE1

echo $PHASE1
