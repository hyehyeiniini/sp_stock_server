#include "csapp.h"

sbuf_t sbuf;
static int byte_cnt = 0;

#define SBUFSIZE 1000
#define NTHREADS 1000

int main(int argc, char **argv){
    Signal(SIGINT, sigint_handler);

    int i, listenfd, connfd;
    FILE * fp;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    
    if (argc != 2){
        fprintf(stderr, "usage : %s <port>\n", argv[0]);
        exit(0);
    }

    // Make tree for stocks
    fp = fopen("stock.txt", "r");
    if (fp == NULL){
        fprintf(stderr, "no stock.txt file\n");
        exit(0);
    }
    root = make_tree(fp);
    fclose(fp);

    listenfd = Open_listenfd(argv[1]);
    sbuf_init(&sbuf, SBUFSIZE);
    for (i = 0; i < NTHREADS; i++){
        Pthread_create(&tid, NULL, thread, NULL);
    }
    while(1){
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        sbuf_insert(&sbuf, connfd);
    }
    Close(listenfd);

    return 0;
}

void sigint_handler(int s){
    int olderrno = errno;
    
    // Write tree into stock.txt
    FILE * fp;
    fp = fopen("stock.txt", "w");
    if (fp == NULL){
        perror("Open file error");
        exit(1);
    }    
    writeTree(root, fp);
    fclose(fp);
    
    exit(0);
    errno = olderrno;
}

void *thread(void *vargp){
    Pthread_detach(pthread_self());
    while(1){
        int connfd = sbuf_remove(&sbuf);
        echo(connfd);
        printf("Close connfd %d\n", connfd);
        Close(connfd);
    }
}

void echo(int connfd) 
{
    int n; 
    int end_flag = 0;
    char buf[MAXLINE]; 
    rio_t rio;
    // static pthread_once_t once = PTHREAD_ONCE_INIT;

    // Pthread_once(&once, init_echo_cnt);
    Rio_readinitb(&rio, connfd);
    while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0 && end_flag != -1) {
        byte_cnt += n;
	    printf("server received %d (%d total) bytes\n", n, byte_cnt);
        end_flag = command_process(root, buf, connfd, n);
    }
}

/* 1) buf로부터 show / buy / sell / exit 명령어를 구분하고
   2) 각 명령어에 맞게 트리 자료구조를 이용하여 명령 수행 */
int command_process(Node* root, char * commandline, int connfd, int n){
    char buf[MAXLINE];
    int ID, ID_num;
    int flag = 0;
    char* ptr;
    char* next_ptr;

    if (!strcmp(commandline, "show\n")){
        // Reader
        int index = 0;
        printTree(root, buf, &index);
        // fprintf(stdout, "show\n");
        Rio_writen(connfd, buf, MAXLINE);
        return 0;
    }
    else if (!strcmp(commandline, "exit\n")){
        Rio_writen(connfd, "", MAXLINE);
        return -1;
    }
    ptr = strtok_r(commandline, " ", &next_ptr);
    if (!strcmp(ptr, "buy")){
        // Writer
        ptr = strtok_r(NULL, " ", &next_ptr);
        ID = atoi(ptr);
        ptr = strtok_r(NULL, " ", &next_ptr);
        ID_num = atoi(ptr);

        // fprintf(stdout, "buy %d %d\n", ID, ID_num);

        flag = treeModify(root, ID, -ID_num);
        if (flag != 0){
            strcpy(buf, "Not enough left stocks\n");
        }
        else strcpy(buf, "[buy] success\n");
        Rio_writen(connfd, buf, MAXLINE);
    }
    else if (!strcmp(ptr, "sell")){
        // Writer
        ptr = strtok_r(NULL, " ", &next_ptr);
        ID = atoi(ptr);
        ptr = strtok_r(NULL, " ", &next_ptr);
        ID_num = atoi(ptr);

        // fprintf(stdout, "sell %d %d\n", ID, ID_num);

        treeModify(root, ID, ID_num);
        strcpy(buf, "[sell] success\n");
        Rio_writen(connfd, buf, MAXLINE);
    }

    return 0;
}

Node* newNode(int ID, int left_stock, int price){
    Node* new = (Node *)malloc(sizeof(Node));
    new->ID = ID;
    new->left_stock = left_stock;
    new->price = price;
    new->readcnt = 0;
    new->left = NULL;
    new->right = NULL;
    sem_init(&(new->mutex), 0, 1);
    return new;
}

Node* insertNode(Node* root, int ID, int left_stock, int price){
    if (root == NULL){
        root = newNode(ID, left_stock, price);
        return root;
    }

    if (ID < root->ID){
        root->left = insertNode(root->left, ID, left_stock, price);
    }
    else if (ID > root->ID){
        root->right = insertNode(root->right, ID, left_stock, price);
    }
    return root;
}

int treeModify(Node* root, int ID, int d_num){

    if (ID < root->ID){
        return treeModify(root->left, ID, d_num);
    }
    else if (ID > root->ID){
        return treeModify(root->right, ID, d_num);
    }

    else{ // ID == root->ID
        P(&root->mutex);
        if (root->left_stock + d_num < 0){
            V(&root->mutex);
            return -1;
        }
        else {
            root->left_stock += d_num;
            V(&root->mutex);
            return 0;
        }
    }
}

void printTree(Node* root, char* buf, int* index) {
    if (root != NULL) {
        printTree(root->left, buf, index);
        P(&root->mutex);
        *index += sprintf(&buf[*index], "%d %d %d\n", root->ID, root->left_stock, root->price);
        V(&root->mutex);
        printTree(root->right, buf, index);
    }
}

void writeTree(Node* root, FILE* fp){
    int fd = fileno(fp);
    char buf[MAXLINE];
    ssize_t b_read;
    if (root != NULL) {
        writeTree(root->left, fp);
        sprintf(buf, "%d %d %d\n", root->ID, root->left_stock, root->price);
        b_read = write(fd, buf, strlen(buf));
        if (b_read == -1){
            fprintf(stderr, "Error writing to file: %s\n", strerror(errno));
            Close(fd);
            return;
        }
        writeTree(root->right, fp);
    }
}

Node* make_tree(FILE* fp){
    int ID;
    int stock_left;
    int price;

    Node* root = NULL;
    while(fscanf(fp, "%d %d %d", &ID, &stock_left, &price) == 3){
        root = insertNode(root, ID, stock_left, price);
    }
    return root;
}
