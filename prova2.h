#include <stdio.h>

#if !defined (PROVA2_H_)
#define PROVA2_H_
static inline void function(){
    extern int ciao;
    printf("%d\n", ciao);
}
static inline void function2(){
    extern int ok;
    printf("%d\n", ok);
}
#endif