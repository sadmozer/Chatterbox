#include <stdarg.h>

#include "list.h"
#include "icl_hash.h"

#if defined(c_plusplus) || defined(__cplusplus)
extern "C" {
#endif

#if !defined (NUOVO_H_)
#define NUOVO_H_

g_list * to_list(icl_hash_t * tab, pthread_mutex_t * mux);
int to_list_vfun(icl_entry_t * corr, void ** argv);
void post_all(icl_hash_t * ht, pthread_mutex_t * mux, char * sender, msg_data_t * message);
int post_all_vfun(icl_entry_t * corr, void ** argv);
char * set_offline(icl_hash_t * ht, pthread_mutex_t * mux, int fd);
int set_offline_vfun(icl_entry_t * corr, void ** argv);

#endif
