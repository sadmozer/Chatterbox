#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <config.h>
#include <parser.h>
#include <options.h>

typedef enum {
    ERROR = -1,
    BEFORE_KEY = 0,
    INSIDE_KEY = 1,
    BEFORE_EQUALS = 2,
    AFTER_EQUALS = 3,
    INSIDE_VALUE = 4,
    AFTER_VALUE = 5
} state_t;
    
extern struct options ops;

static int decode_key(char * key){
    if(strcmp(key, "UnixPath") == 0){
        //printf("Chiave: UnixPath\n");
        return 0;
    }
    if(strcmp(key, "MaxConnections") == 0){
        //printf("Chiave: MaxConnections\n");
        return 1;
    }
    if(strcmp(key, "ThreadsInPool") == 0){
        //printf("Chiave: ThreadsInPool\n");
        return 2;
    }
    if(strcmp(key, "MaxMsgSize") == 0){
        //printf("Chiave: MaxMsgSize\n");
        return 3;
    }
    if(strcmp(key, "MaxFileSize") == 0){
        //printf("Chiave: MaxFileSize\n");
        return 4;
    }
    if(strcmp(key, "MaxHistMsgs") == 0){
        //printf("Chiave: MaxHistMsgs\n");
        return 5;
    }
    if(strcmp(key, "DirName") == 0){
        //printf("Chiave: DirName\n");
        return 6;
    }
    if(strcmp(key, "StatFileName") == 0){
        //printf("Chiave: StatFileName\n");
        return 7;
    }
    //printf("Chiave Sconosciuta\n");
    return -1;
}
static int set_value(char * value, int index){
    if(index == -1 || index > 7)
        return -1;
    switch(index){
        case 0: {
            // char copy[MAX_PATH_LENGTH];
            // int flag = 0;
            // int index;
            // int c;
            // for(c = (int) strlen(value); c >= 0; c--){
            //     //printf("%c", value[c]);
            //     if(value[c] == '/'){
            //         flag = 1;
            //         index = c;
            //     }
            //     if(flag == 1)
            //         copy[c] = value[c];
            // }
            // copy[index] = '\0';
            // // printf("\ncopy %s\n", copy);
            // // int aux = mkdir(copy, 0777);
			// if(aux == -1 && errno != EEXIST){
            //     perror("Parser: mkdir");                
			// 	return -1;
			// }
            // else if(errno == EEXIST)
			// 	printf("Parser: UnixPath Directory already exists\n");
            // else
            //     printf("Parser: UnixPath Directory successfully created\n");
            memset(ops.UnixPath, 0, sizeof(char) * MAX_PATH_LENGTH);
			if((int) strlen(value) > MAX_PATH_LENGTH){
				fprintf(stderr, "Parser: UnixPath path is too long");
				return -1;
			}
            strcpy(ops.UnixPath, value);
            // printf("Parser: UnixPath setting -> %s\n", ops.UnixPath);
        } break;
        case 1: {
            int aux = atoi(value);
            if(aux <= 0){
                perror("Parser: atoi");
				return -1;
			}
			if(aux > MAX_CONNECTION_NUMBER || aux < 1){
				fprintf(stderr, "Parser: MaxConnections value is out of range (must be between 1 and %d)\n", MAX_CONNECTION_NUMBER);
				return -1;
			}
            ops.MaxConnections = aux;
        } break;
        case 2: {
            int aux = atoi(value);
            if(aux <= 0){
                perror("Parser: atoi");
				return -1;
			}
			if(aux > MAX_THREADPOOL_NUMBER || aux < 1){
				fprintf(stderr, "Parser: ThreadInPool value is out of range (must be between 1 and %d)\n", MAX_THREADPOOL_NUMBER);
				return -1;
			}
            ops.ThreadsInPool = aux;
        } break;
        case 3: {
            int aux = atoi(value);
            if(aux <= 0){
				perror("Parser: atoi");
				return -1;
			}
			if(aux > MAX_MSG_SIZE_NUMBER || aux < 1){
				fprintf(stderr, "Parser: MaxMsgSize value is out of range (must be between 1 and %d)\n", MAX_MSG_SIZE_NUMBER);
				return -1;
			}
            ops.MaxMsgSize = aux;
        } break;
        case 4: {
            int aux = atoi(value);
            if(aux <= 0){
                perror("Parser: atoi");
				return -1;
			}
			if(aux > MAX_FILE_KB || aux < 1){
				fprintf(stderr, "Parser: MaxFileSize value is out of range (must be between 1 and %d)\n", MAX_FILE_KB);				
				return -1;
			}
			ops.MaxFileSize = aux * 1024;
        } break;
        case 5: {
            int aux = atoi(value);
            if(aux <= 0){
                perror("Parser: atoi");
				return -1;
			}
			if(aux > MAX_HIST_LENGHT || aux < 1){
				fprintf(stderr, "Parser: MaxHistMsgs value is out of range (must be between 1 and %d)\n", MAX_HIST_LENGHT);								
				return -1;
			}
            ops.MaxHistMsgs = aux;
        } break;
        case 6: {
			int aux = mkdir(value, 0777);
			if(aux == -1 && errno != EEXIST){
                perror("Parser: mkdir");                
				return -1;
			}
            else if(errno == EEXIST)
				printf("Parser: DirName Directory already exists\n");
            else
                printf("Parser: DirName Directory successfully created\n");
            memset(ops.DirName, 0, sizeof(char) * MAX_PATH_LENGTH);
			strcpy(ops.DirName, value);
        } break;
        case 7: {
            FILE * aux;
			if((int) strlen(value) > MAX_PATH_LENGTH){
				fprintf(stderr, "Parser: StatFileName path is too long\n");
				return -1;
			}
            if((aux = fopen(value, "wb")) == NULL){
                perror("Parser: fopen");
                return -1;
            }
            fclose(aux);
            memset(ops.StatFileName, 0, sizeof(char) * MAX_PATH_LENGTH);
            strcpy(ops.StatFileName, value);
        } break;
    }
    return 0;
}
int parser(char * path){
    FILE * filep;
    setOptions(DF_UNIXPATH, DF_MAXCONNECTIONS, DF_THREADSINPOOL, DF_MAXMSGSIZE, 
    DF_MAXFILESIZE, DF_MAXHISTMSGS, DF_DIRNAME, DF_STATFILENAME);
    //printf("Parser: In caso di impostazioni mancanti o errate, tali opzioni verranno settate con i valori di default (vedi config.h)\n");
    if((filep = fopen(path, "r")) == NULL){
        perror("fopen");
        return -1;
    }
    fseek(filep, 0L, SEEK_END);
    size_t size = ftell(filep);
    fseek(filep, 0L, SEEK_SET);
    
    size_t i = 0;
    int curr_line = 0;
    int curr_char = 0;
    state_t curr_state = BEFORE_KEY;
	
    int error = 0;
    int comment = 0;
    int k = 0;
    int j = 0;
	int index = -1;
    
    char key[30];
    char value[MAX_PATH_LENGTH];
    memset(key, 0, sizeof(char)*30);
    memset(value, 0, sizeof(char) * MAX_PATH_LENGTH);
    
    while (i < size)
    {
        curr_char = fgetc(filep);
        i++;

        switch (curr_state) 
		{
        case BEFORE_KEY :    
            switch (curr_char){
            case '#': 
                comment = 1; 
                break;
            case '\n': 
            { 
                comment = 0; 
                curr_line++;
                curr_state = BEFORE_KEY; 
            } break;
            case ' ' :
            case '	' :
                curr_state = BEFORE_KEY;
            case EOF: break;
            case '=' : 
            {
                if (comment == 0)
                    curr_state = ERROR;
                else
                    curr_state = BEFORE_KEY;
            } break;
            default : 
            { 
                if (comment == 0){
                    char aux = (char) curr_char;
                    strncpy(key + k * sizeof(char), &aux, sizeof(char));
                    k++;
                    curr_state = INSIDE_KEY;
                }
                else
                    curr_state = BEFORE_KEY;
            } break;
            }
        break;
        case INSIDE_KEY :
            switch (curr_char) {
            case ' ' :
            case '	' :
            {
                key[k] = '\0';
                if ((index = decode_key(key)) == -1)
                    curr_state = ERROR;
                else
                    curr_state = BEFORE_EQUALS;
                k = 0;
                memset(key, 0, sizeof(char) * 30);
            } break;
            case '=': {
                key[k] = '\0';
                if ((index = decode_key(key)) == -1)
                    curr_state = ERROR;
                else 
                    curr_state = AFTER_EQUALS;
                k = 0;
                memset(key, 0, sizeof(char) * 30);
            } break;
            case '\n' : {
                key[k] = '\0';
                k = 0;
                memset(key, 0, sizeof(char) * 30);
                error = 1;                                
                curr_line++;
                curr_state = BEFORE_KEY;
            } break;
            case '#' : {
                curr_state = ERROR;
            } break;
            case EOF: 
                break;
            default:
            {
                char aux = (char) curr_char;
                strncpy(key + k * sizeof(char), &aux, sizeof(char));
                k++;
                curr_state = INSIDE_KEY;
            } break;
            }
        break;
        case BEFORE_EQUALS :
            switch (curr_char) {
            case ' ' :
            case '	' :
				curr_state = BEFORE_EQUALS;
                break;
            case '=': {
                curr_state = AFTER_EQUALS;
            } break;
            case '#' : {
                curr_state = ERROR;
            } break;
            case '\n' : {
                curr_line++;
                error = 1;
                curr_state = BEFORE_KEY;
            }
            case EOF :
                break;
            default:
            {
                curr_state = ERROR;
            } break;
            }
        break;
        case AFTER_EQUALS :
            switch(curr_char){
                case ' ' :
                case '	' :
                    break;
                case EOF :
                    break;
                case '#' : {
                    curr_state = ERROR;
                } break;
                case '\n' : {
                    curr_state = BEFORE_KEY;
                } break;
                default : {
                    char aux = (char) curr_char;
					strncpy(value + j * sizeof(char), &aux, sizeof(char));
                    j++;
                    curr_state = INSIDE_VALUE;
                } break;
            }
		break;
        case INSIDE_VALUE :
            switch (curr_char) {
            case ' ' :
            case '	' :
            {
                value[j] = '\0';
                if (set_value(value, index) == -1)
                    curr_state = ERROR;
                else
                    curr_state = AFTER_VALUE;
                j = 0;
                memset(value, 0, sizeof(char) * MAX_PATH_LENGTH);
            } break;
            case '\n':
            {
                value[j] = '\0';
                if (set_value(value, index) == -1){
                    error = 1;
                }
                j = 0;
                curr_line++;
                curr_state = BEFORE_KEY;    
                memset(value, 0, sizeof(char) * MAX_PATH_LENGTH);
            } break;
            case EOF :
                break;
            case '#' : {
                curr_state = ERROR;
            } break;
            default :
            {				
                char aux = (char) curr_char;
                strncpy(value + j * sizeof(char), &aux, sizeof(char));
                j++;
                curr_state = INSIDE_VALUE;
            } break;
            }
        break;
        case AFTER_VALUE:
            switch (curr_char) {
            case ' ' :
            case '	' :
                break;
            case '\n':
            {
                curr_line++;
                curr_state = BEFORE_KEY;
            } break;
            case EOF:
                break;
            default: 
			{
                error = 1;
				curr_state = ERROR;
                break;
            }
            }
		break;
        case ERROR :
            switch(curr_char) {
                case '\n': 
				{
					curr_state = BEFORE_KEY; 
					curr_line++;
				} break;
                case EOF : break;
                default : break;
            }
		break; 
	}
	if(error == 1){
        fprintf(stderr, "Parser: Error line %d\n", curr_line);
        error = 0;
    }
    }
    fclose(filep);
    return 0;
}