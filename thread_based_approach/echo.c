/*
 * echo - read and echo text lines until client closes connection
 */
/* $begin echo */
#include "csapp.h"

static int byte_cnt;
static sem_t mutex;
static void init_echo_cnt(void){
    sem_init(&mutex, 0, 1);
    byte_cnt = 0;
}

void echo(int connfd) 
{
    int n; 
    int end_flag = 0;
    char buf[MAXLINE]; 
    rio_t rio;
    static pthread_once_t once = PTHREAD_ONCE_INIT;

    Pthread_once(&once, init_echo_cnt);
    Rio_readinitb(&rio, connfd);
    while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0 && end_flag != -1) {
        byte_cnt += n;
	    printf("server received %d (%d total) bytes\n", n, byte_cnt);
        end_flag = command_process(root, buf, connfd, n);
    }
}

/* $end echo */

