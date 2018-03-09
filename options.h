#if !defined (OPTIONS_H_)
#define OPTIONS_H_
#include <string.h>
#include <stdio.h>
#include <config.h>

struct options {
    char UnixPath[MAX_PATH_LENGTH];
    unsigned int MaxConnections;
    unsigned int ThreadsInPool;
    unsigned int MaxMsgSize;
    unsigned int MaxFileSize;
    unsigned int MaxHistMsgs;
    char DirName[MAX_PATH_LENGTH]; 
    char StatFileName[MAX_PATH_LENGTH];
};

static inline void setOptions(char * up, int mc, int tip, int mms, int mfs, int mhm, char * dn, char * sfn){
    extern struct options ops;
    strcpy(ops.UnixPath, up);
    strcpy(ops.DirName, dn);
    strcpy(ops.StatFileName, sfn);
    ops.MaxConnections = mc;
	ops.ThreadsInPool = tip;
	ops.MaxHistMsgs = mhm;
	ops.MaxMsgSize = mms;
	ops.MaxFileSize = mfs;
}
static inline void printOptions(){
    extern struct options ops;
    printf("UnixPath setting -> %s\n", ops.UnixPath);
    printf("MaxConnections setting -> %d\n", ops.MaxConnections);
    printf("ThreadInPool setting -> %d\n", ops.ThreadsInPool);
    printf("MaxMsgSize setting -> %d\n", ops.MaxMsgSize);
    printf("MaxFileSize setting -> %d\n", ops.MaxFileSize);
    printf("MaxHistMsgs setting -> %d\n", ops.MaxHistMsgs);
    printf("DirName setting -> %s\n", ops.DirName);
    printf("StatFileName setting -> %s\n", ops.StatFileName);
}

#endif