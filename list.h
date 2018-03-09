#include <pthread.h>

#include "ops.h"

#if !defined (LIST_H_)
#define LIST_H_

typedef struct icl_entry_s {
    void* key;
    void *data;
    struct icl_entry_s* next;
} icl_entry_t;

typedef struct g_list{
    icl_entry_t * head;        /* Pointer to the first element in the list */
    icl_entry_t * tail;        /* Pointer to the last element in the list */
    int num;            /* Number of elements in the list */
    int max;
    pthread_mutex_t mux;
    int (*compare)(void *, void *);
    void (*free_key)(void *);
    void (*free_data)(void *);
} g_list;

/* ---- DATA ---- */

typedef struct user_data_t {
        int fd;
        g_list * pvmsg;
} user_data_t;

typedef struct msg_data_t {
    op_t type;
    char * buf;
    int len;
} msg_data_t;


int string_compare(void * a, void * b);
int int_compare(void * a, void * b);

char * new_string(char * string);
int * new_integer(int integer);
g_list * new_glist(int max, int (*compare)(void *, void *), void (*free_data)(void *), void (*free_key)(void *));
user_data_t * new_usr_data(int fd, int max);
msg_data_t * new_msg_data(op_t type, char * buf, int len);

void free_pointer(void * pointer);
void free_data_usr(void * arg);
void free_data_msg(void * arg);

void destroy_g(g_list * list);
void add_g(g_list * list, void * key, void * data);
icl_entry_t * remove_g(g_list * list, void * key);
icl_entry_t * remove2_g(g_list * list, int pos);
// icl_entry_t * get_g(g_list * list, int pos);
// int getind_g(g_list * list, void * arg);
// int getind2_g(g_list * list, int arg);


#endif