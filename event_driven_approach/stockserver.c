#include "csapp.h"

int byte_cnt = 0;

int main(int argc, char **argv){
    Signal(SIGINT, sigint_handler);

    int listenfd, connfd;
    FILE * fp;
    socklen_t clientlen;
    static pool pool;
    struct sockaddr_storage clientaddr;
    
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
    init_pool(listenfd, &pool);
    while(1){
        pool.ready_set = pool.read_set;
        pool.nready = Select(pool.maxfd+1, &pool.ready_set, NULL, NULL, NULL);
        if (pool.nready == -1){
            break;
        }
        if (FD_ISSET(listenfd, &pool.ready_set)){
            clientlen = sizeof(struct sockaddr_storage);
            connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
            add_client(connfd, &pool);
        }
        check_clients(&pool, root);
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


void init_pool(int listenfd, pool *p){
    int i;
    p->maxi = -1;
    for (i = 0; i < FD_SETSIZE; i++){
        p->clientfd[i] = -1;
    }
    p->maxfd = listenfd;
    FD_ZERO(&p->read_set);
    FD_SET(listenfd, &p->read_set);
}

void add_client(int connfd, pool *p){
    int i;
    p->nready--; // nready = number of ready descriptor. // 왜 줄임??
    for (i = 0; i <FD_SETSIZE; i++){
        if (p->clientfd[i] < 0){
            /* Add connected descriptor to the pool */
            p->clientfd[i] = connfd;
            Rio_readinitb(&p->clientrio[i], connfd);

            /* Add the descriptor to descriptor set */
            FD_SET(connfd, &p->read_set);

            /* Update max descriptor and pool high water mark */
            if (connfd > p->maxfd){
                p->maxfd = connfd;
            }
            if (i > p->maxi){
                p->maxi = i;
            }
            break;
        }
    }
    if (i == FD_SETSIZE){
        app_error("add_client error : Too many clients");
    }
}

void check_clients(pool *p, Node* root){
    int i, connfd, n;
    int end_flag = 0;
    char buf[MAXLINE];
    rio_t rio;

    for (i = 0; (i <= p->maxi) && (p->nready > 0); i++){
        connfd = p->clientfd[i];
        rio = p->clientrio[i];

        /* If the descriptor is ready, echo a text line from it */
        if ((connfd > 0) && (FD_ISSET(connfd, &p->ready_set))){
            p->nready--; // 왜?? 모르겠음
            if ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0){
                byte_cnt += n;
                printf("Server received %d (%d total) bytes on fd %d\n", n, byte_cnt, connfd);
                end_flag = command_process(root, buf, connfd, n);
            }
            if (n == 0 || end_flag == -1){
                printf("Close connfd %d\n", connfd);
                Close(connfd);
                FD_CLR(connfd, &p->read_set);
                p->clientfd[i] = -1;
            }
        }
    }
}

/* 1) buf로부터 show / buy / sell / exit 명령어를 구분하고
   2) 각 명령어에 맞게 트리 자료구조를 이용하여 명령 수행 */
int command_process(Node* root, char * commandline, int connfd, int n){
    char buf[MAXLINE];
    int ID, ID_num;
    int flag = 0;
    char* ptr;

    if (!strcmp(commandline, "show\n")){
        // Tree 보여주기
        int index = 0;
        printTree(root, buf, &index);
        Rio_writen(connfd, buf, MAXLINE);
        return 0;
    }
    else if (!strcmp(commandline, "exit\n")){
        rio_writen(connfd, "", MAXLINE);
        return -1;
    }
    ptr = strtok(commandline, " ");
    if (!strcmp(ptr, "buy")){
        ptr = strtok(NULL, " ");
        ID = atoi(ptr);
        ptr = strtok(NULL, " ");
        ID_num = atoi(ptr);

        flag = treeModify(root, ID, -ID_num);
        if (flag != 0){
            strcpy(buf, "Not enough left stocks\n");
        }
        else strcpy(buf, "[buy] success\n");
        Rio_writen(connfd, buf, MAXLINE);
    }
    else if (!strcmp(ptr, "sell")){
        ptr = strtok(NULL, " ");
        ID = atoi(ptr);
        ptr = strtok(NULL, " ");
        ID_num = atoi(ptr);

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
    if (root->ID == ID){
        if (root->left_stock + d_num < 0){
            return -1;
        }
        else {
            root->left_stock += d_num;
            return 0;
        }
    }

    else if (ID < root->ID){
        return treeModify(root->left, ID, d_num);
    }
    else{
        return treeModify(root->right, ID, d_num);
    }
}

void printTree(Node* root, char* buf, int* index) {
    if (root != NULL) {
        printTree(root->left, buf, index);
        *index += sprintf(&buf[*index], "%d %d %d\n", root->ID, root->left_stock, root->price);
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
