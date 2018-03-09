#!/bin/bash

if [[ $# != 1 ]]; then
    echo "usa $0 unix_path"
    exit 1
fi
/usr/bin/valgrind -v --leak-check=full ./chatty -f DATA/chatty.conf1 >& ./valgrind_out &
pid=$!
# ./chatty -f DATA/chatty.conf1 &
#pid=$!
# sleep 5

./client -l $1 -c paperino
./client -l $1 -c pippo
# ./client -l $1 -k pippo -S misenti?:
#  ./client -l $1 -k pippo -S misenti2?:
# ./client -l $1 -k paperino -C paperino
# ./client -l $1 -k pippo -S misenti2?:
./client -l $1 -c topolino
# ./client -l $1 -c topolino
./client -l $1 -k topolino -R 1 &
# ./client -l $1 -k pippo -S allora?:paperino
./client -l $1 -k pippo -S mitradisci???:
# ./client -l $1 -k paperino -S ciao:topolino -S "come va":topolino
# ./client -l $1 -k pippo -R 1 &
# ./client -l $1 -k topolino -L
# ./client -l $1 -k paperino -S civediamostasera:
./client -l $1 -k paperino -p
# ./client -l $1 -k paperino -S ???:
# ./client -l $1 -k paperino -S machedici:pippo
# ./client -l $1 -k pippo -p
# ./client -l $1 -k topolino -p
#rm /tmp/chatty_socket
# registro un po' di nickname
# ./client -l $1 -c pippo &
# ./client -l $1 -c pluto &
# ./client -l $1 -c minni &
# ./client -l $1 -c topolino &
# ./client -l $1 -c paperino &
# ./client -l $1 -c qui &
# ./client -l $1 -c quo &
# ./client -l $1 -c qua &

kill -TERM $pid
# #minni deve ricevere 8 messaggi prima di terminare
# ./client -l $1 -k minni -R 8 &
# pid=$!

# # aspetto un po' per essere sicuro che il client sia partito
# sleep 1

# # primo messaggio
# ./client -l $1 -k topolino -S "ciao da topolino":minni
# if [[ $? != 0 ]]; then
#     exit 1
# fi
# # secondo e terzo
# ./client -l $1 -k paperino -S "ciao da paperino":minni -S "ciao ciao!!!":minni
# if [[ $? != 0 ]]; then
#     exit 1
# fi
# # quarto
# ./client -l $1 -k qui -S "ciao a tutti":
# if [[ $? != 0 ]]; then
#     exit 1
# fi
# # quinto e sesto
# ./client -l $1 -k quo -S "ciao a tutti":  -S "ciao da quo":minni
# if [[ $? != 0 ]]; then
#     exit 1
# fi
# # settimo ed ottavo
# ./client -l $1 -k qua -S "ciao a tutti":  -S "ciao da qua":minni -p
# if [[ $? != 0 ]]; then
#     exit 1
# fi

# wait $pid
# if [[ $? != 0 ]]; then
#     echo "ESCO8"
#     exit 1
# fi

# # messaggio di errore che mi aspetto
# OP_NICK_ALREADY=26

# # provo a ri-registrare pippo
# ./client -l $1 -c pippo
# e=$?
# if [[ $((256-e)) != $OP_NICK_ALREADY ]]; then
#     echo "Errore non corrispondente $e"
#     exit 1
# fi
