#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

/* inserire gli altri include che servono */
#include <sys/select.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/syscall.h>


#include <connections.h>
#include <message.h>
#include <ops.h>
#include <stats.h>
#include <config.h>
#include <list.h>
#include <icl_hash.h>
#include <handler.h>

void handler(int signum){
    eflag = 1;
    if(signum == SIGINT)
    pthread_cond_broadcast(&cond_taskq);
}
void * handler_worker(void * arg){
    sigset_t mask;
    sigemptyset(&mask);
    pthread_sigmask(SIG_SETMASK,&mask,NULL);
    int sig;
    sigfillset(&mask);
    sigwait(&mask, &sig);
    printf("Handler: SIGINT catturato\n");
    return NULL;
}

