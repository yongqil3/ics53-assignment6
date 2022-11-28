// Qiwei He 47771452 and Liwei Lu 90101531

#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <pthread.h>
#include "Md5.c"

#define NTHREADS 4
#define SBUFSIZE 16
#define MAXLINE 8192 /* Max text line length */
#define MAXBUF 8192  /* Max I/O buffer size */
#define LISTENQ 1024 /* Second argument to listen() */

#define APPENDING "a+"
#define READING "r"

#define EMPTY_STR ""

int open_status; // 1 -> open 0 ->close

typedef struct
{
    int *buf;    /* Buffer array */
    int n;       /* Maximum number of slots */
    int front;   /* buf[(front+1)%n] is first item */
    int rear;    /* buf[rear%n] is last item */
    sem_t mutex; /* Protects accesses to buf */
    sem_t slots; /* Counts available slots */
    sem_t items; /* Counts available items */
} sbuf_t;
/* $end sbuft */

typedef struct
{
    char filename[MAXLINE];
    FILE *fd;
    sem_t mutex[2]; // mutex[0] is for reading, mutex[1] is for writing
    int read_ref;   // number of reference
} OFT_Entry;

void print_hash(unsigned char *digest)
{
    unsigned char *temp = digest;
    printf("Hash: ");
    while (*temp)
    {
        printf("%02x", *temp);
        temp++;
    }
    printf("\n");
}

void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);

/* Create an empty, bounded, shared FIFO buffer with n slots */
/* $begin sbuf_init */
void sbuf_init(sbuf_t *sp, int n)
{
    sp->buf = calloc(n, sizeof(int));
    sp->n = n;                  /* Buffer holds max of n items */
    sp->front = sp->rear = 0;   /* Empty buffer iff front == rear */
    sem_init(&sp->mutex, 0, 1); /* Binary semaphore for locking */
    sem_init(&sp->slots, 0, n); /* Initially, buf has n empty slots */
    sem_init(&sp->items, 0, 0); /* Initially, buf has zero data items */
}
/* $end sbuf_init */

/* Clean up buffer sp */
/* $begin sbuf_deinit */
void sbuf_deinit(sbuf_t *sp)
{
    free(sp->buf);
}
/* $end sbuf_deinit */

/* Insert item onto the rear of shared buffer sp */
/* $begin sbuf_insert */
void sbuf_insert(sbuf_t *sp, int item)
{
    sem_wait(&sp->slots);                   /* Wait for available slot */
    sem_wait(&sp->mutex);                   /* Lock the buffer */
    sp->buf[(++sp->rear) % (sp->n)] = item; /* Insert the item */
    sem_post(&sp->mutex);                   /* Unlock the buffer */
    sem_post(&sp->items);                   /* Announce available item */
}
/* $end sbuf_insert */

/* Remove and return the first item from buffer sp */
/* $begin sbuf_remove */
int sbuf_remove(sbuf_t *sp)
{
    int item;
    sem_wait(&sp->items);                    /* Wait for available item */
    sem_wait(&sp->mutex);                    /* Lock the buffer */
    item = sp->buf[(++sp->front) % (sp->n)]; /* Remove the item */
    sem_post(&sp->mutex);                    /* Unlock the buffer */
    sem_post(&sp->slots);                    /* Announce available slot */
    return item;
}

int open_listenfd(const char *port)
{
    struct addrinfo hints, *listp, *p;
    int listenfd, rc, optval = 1;

    /* Get a list of potential server addresses */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;             /* Accept connections */
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG; /* ... on any IP address */
    hints.ai_flags |= AI_NUMERICSERV;            /* ... using port number */
    if ((rc = getaddrinfo(NULL, port, &hints, &listp)) != 0)
    {
        fprintf(stderr, "getaddrinfo failed (port %s): %s\n", port, gai_strerror(rc));
        return -2;
    }

    /* Walk the list for one that we can bind to */
    for (p = listp; p; p = p->ai_next)
    {
        /* Create a socket descriptor */
        if ((listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
            continue; /* Socket failed, try the next */

        /* Eliminates "Address already in use" error from bind */
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, // line:netp:csapp:setsockopt
                   (const void *)&optval, sizeof(int));

        /* Bind the descriptor to the address */
        if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
            break; /* Success */
        if (close(listenfd) < 0)
        { /* Bind failed, try the next */
            fprintf(stderr, "open_listenfd close failed: %s\n", strerror(errno));
            return -1;
        }
    }

    /* Clean up */
    freeaddrinfo(listp);
    if (!p) /* No address worked */
        return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, LISTENQ) < 0)
    {
        close(listenfd);
        return -1;
    }
    return listenfd;
}

sbuf_t sbuf; /* Shared buffer of connected descriptors */
OFT_Entry file_table[4];
sem_t OFT_mutex;

void echo(int connfd);
void *thread(void *vargp);

void OFT_init()
{
    int i;
    sem_init(&OFT_mutex, 0, 1);
    for (i = 0; i < NTHREADS; i++)
    {
        file_table[i].read_ref = 0;
        sem_init(&file_table[i].mutex[0], 0, 1);
        sem_init(&file_table[i].mutex[1], 0, 1);
    }
}

// return an index in OFT, -1 for error
int openRead(char *filename)
{
    int i;
    int index;
    int svalue;
    sem_getvalue(&OFT_mutex, &svalue);
    sem_wait(&OFT_mutex);
    for (i = 0; i < NTHREADS; i++)
    {
        sem_getvalue(&file_table[i].mutex[1], &svalue);
        if (file_table[i].read_ref == 0 && svalue == 1)
            index = i;
        if (strcmp(file_table[i].filename, filename) == 0)
        {
            if (svalue == 0 && file_table[i].read_ref == 0)
            { // the file is opened for appending
                sem_post(&OFT_mutex);
                return -1;
            }
            index = i;
            break;
        }
    }

    if (file_table[index].read_ref == 0)
    {
        // need to open file
        file_table[index].fd = fopen(filename, READING);
        strcpy(file_table[index].filename, filename);
        // get the write lock
        sem_wait(&file_table[index].mutex[1]);
    }
    file_table[index].read_ref++;
    sem_post(&OFT_mutex);

    return index;
}

// return an index in OFT, -1 for error
int openAppend(char *filename)
{
    int i;
    int index;
    int svalue;
    sem_wait(&OFT_mutex);
    for (i = 0; i < NTHREADS; i++)
    {
        sem_getvalue(&file_table[i].mutex[1], &svalue);
        if (file_table[i].read_ref == 0 && svalue == 1)
            index = i;
        if (strcmp(file_table[i].filename, filename) == 0)
        {
            sem_post(&OFT_mutex);
            return -1;
        }
    }
    // get the write lock
    sem_wait(&file_table[index].mutex[1]);
    file_table[index].fd = fopen(filename, APPENDING);
    strcpy(file_table[index].filename, filename);

    sem_post(&OFT_mutex);

    return index;
}

void read_file(char *buf, int size, int OFT_index, int position)
{
    sem_wait(&OFT_mutex);
    fseek(file_table[OFT_index].fd, position, SEEK_SET);
    fgets(buf, size + 1, file_table[OFT_index].fd);
    sem_post(&OFT_mutex);
    return;
}

void append_file(char *buf, int OFT_index)
{
    fputs(buf, file_table[OFT_index].fd);
    return;
}

void close_file(int OFT_index)
{
    // printf("Closing File on index %d which has %d client in use.\n", OFT_index, file_table[OFT_index].read_ref);
    sem_wait(&OFT_mutex);
    if (file_table[OFT_index].read_ref > 1)
    {
        file_table[OFT_index].read_ref--;
        sem_post(&OFT_mutex);
        return;
    }
    if (file_table[OFT_index].read_ref <= 1)
    {
        file_table[OFT_index].read_ref = 0;
        fclose(file_table[OFT_index].fd);
        memset(&file_table[OFT_index].filename, 0, MAXLINE);
        sem_post(&file_table[OFT_index].mutex[1]);
        sem_post(&OFT_mutex);
        return;
    }
}

void *thread(void *vargp)
{
    pthread_detach(pthread_self());
    while (1)
    {
        int connfd = sbuf_remove(&sbuf); /* Remove connfd from buffer */ // line:conc:pre:removeconnfd
        echo(connfd);                                                    /* Service client */
        // printf("Connected\n");
        close(connfd);
    }
};

void echo(int connfd)
{
    size_t n;
    int i = 0;
    char input[MAXLINE];
    char *filename;
    char *buffer;
    char *spliter = " \n";
    char output[MAXLINE + 1];
    char buf2[MAXLINE];
    int opened;
    int index;
    int read_position;
    opened = 0;
    read_position = 0;
    while ((n = read(connfd, input, MAXLINE)) != 0)
    {
        printf("%s\n", input);
        buffer = strtok(input, spliter);

        if (strcmp(buffer, "getHash") == 0)
        {
            filename = strtok(NULL, spliter);
            printf("Received getHash from connfd %d for file %s\n", connfd, filename);
            unsigned char digest[16]; //  a buffer to store the hash into
            MDFile(filename, digest); // calculate the hash again)

            if (open_status == 0)
            {
                print_hash(digest);
                unsigned char *hash = digest;
                sprintf(buf2, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n", hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6], hash[7], hash[8], hash[9], hash[10], hash[11], hash[12], hash[13], hash[14], hash[15]);
                sprintf(output, "%s", buf2);
                write(connfd, output, strlen(output) + 1);
                for (i = 0; i < MAXLINE; i++)
                {
                    input[i] = '\0';
                    output[i] = '\0';
                    output[i + 1] = '\0';
                    buf2[i] = '\0';
                }
                continue;
            }
            else
            {
                strcpy(buf2, "A file is already open for appending\n");
                sprintf(output, "%s", buf2);
                write(connfd, output, strlen(output) + 1);
                for (i = 0; i < MAXLINE; i++)
                {
                    input[i] = '\0';
                    output[i] = '\0';
                    output[i + 1] = '\0';
                    buf2[i] = '\0';
                }
                continue;
            }
        }

        if (strcmp(buffer, "openRead") == 0)
        {
            if (opened)
            {
                sem_wait(&OFT_mutex);
                if (file_table[index].read_ref)
                {
                    strcpy(buf2, "A file is already open for reading\n");
                }
                else
                {
                    strcpy(buf2, "A file is already open for appending\n");
                }
                sem_post(&OFT_mutex);
                sprintf(output, "%s", buf2);
                printf("%s", buf2);
                write(connfd, output, strlen(output) + 1);
                for (i = 0; i < MAXLINE; i++)
                {
                    input[i] = '\0';
                    output[i] = '\0';
                    output[i + 1] = '\0';
                    buf2[i] = '\0';
                }
                continue;
            }
            filename = strtok(NULL, spliter);
            index = openRead(filename);
            if (index == -1)
            {
                strcpy(buf2, "The file is open by another client.\n");
                sprintf(output, "%s", buf2);
                printf("%s", buf2);
                write(connfd, output, strlen(output) + 1);
                for (i = 0; i < MAXLINE; i++)
                {
                    input[i] = '\0';
                    output[i] = '\0';
                    output[i + 1] = '\0';
                    buf2[i] = '\0';
                }
                continue;
            }
            // file_table[0].fd = fopen(filename, "r");
            opened = 1;
            strcpy(buf2, ""); // file opened
            sprintf(output, "%s", buf2);
            write(connfd, output, strlen(output) + 1);
            for (i = 0; i < MAXLINE; i++)
            {
                input[i] = '\0';
                output[i] = '\0';
                output[i + 1] = '\0';
                buf2[i] = '\0';
            }
            continue;
        }
        if (strcmp(buffer, "openAppend") == 0)
        {
            open_status = 1;
            if (opened)
            {
                for (i = 0; i < MAXLINE; i++)
                {
                    input[i] = '\0';
                    output[i] = '\0';
                    output[i + 1] = '\0';
                    buf2[i] = '\0';
                }
                sem_wait(&OFT_mutex);
                if (file_table[index].read_ref)
                {
                    strcpy(buf2, "A file is already open for reading\n");
                }
                else
                {
                    strcpy(buf2, "A file is already open for appending\n");
                }
                sem_post(&OFT_mutex);
                sprintf(output, "%s", buf2);
                printf("%s", buf2);
                write(connfd, output, strlen(output) + 1);
                for (i = 0; i < MAXLINE; i++)
                {
                    input[i] = '\0';
                    output[i] = '\0';
                    output[i + 1] = '\0';
                    buf2[i] = '\0';
                }
                continue;
            }
            filename = strtok(NULL, spliter);
            index = openAppend(filename);
            if (index == -1)
            {
                strcpy(buf2, "The file is open by another client.\n");
                sprintf(output, "%s", buf2);
                printf("%s", buf2);
                write(connfd, output, strlen(output) + 1);
                for (i = 0; i < MAXLINE; i++)
                {
                    input[i] = '\0';
                    output[i] = '\0';
                    output[i + 1] = '\0';
                    buf2[i] = '\0';
                }
                continue;
            }
            opened = 1;
            strcpy(output, ""); // file opened
            write(connfd, output, strlen(output) + 1);
            // file_table[0].fd = fopen(filename, "a+");
            for (i = 0; i < MAXLINE; i++)
            {
                input[i] = '\0';
                output[i] = '\0';
                output[i + 1] = '\0';
                buf2[i] = '\0';
            }
            continue;
        }

        if (strcmp(buffer, "read") == 0)
        {
            buffer = strtok(NULL, spliter);
            int readlen = atoi(buffer);
            for (i = 0; i < MAXLINE; i++)
            {
                input[i] = '\0';
                output[i] = '\0';
                output[i + 1] = '\0';
                buf2[i] = '\0';
            }
            if (opened == 0)
            {
                strcpy(output, "File not open\n");
                printf("File not open\n");
            }
            else
            {
                read_file(buf2, readlen, index, read_position);
                read_position += readlen;
                // fgets(buf2, readlen, file_table[0].fd);
                sprintf(output, "%s\n", buf2);
            }
        }

        if (strcmp(buffer, "append") == 0)
        {
            buffer = strtok(NULL, spliter);
            if (opened == 0)
            {
                strcpy(output, "File not open\n");
                printf("File not open\n");
            }
            else
            {
                append_file(buffer, index);
                strcpy(output, EMPTY_STR);
            }
            // fputs(buffer, file_table[0].fd);
        }

        if (strcmp(buffer, "close") == 0)
        {
            close_file(index);
            opened = 0;
            open_status = 0;
            read_position = 0;
            strcpy(output, EMPTY_STR);
        }

        write(connfd, output, strlen(output) + 1);
        for (i = 0; i < MAXLINE; i++)
        {
            input[i] = '\0';
            output[i] = '\0';
            output[i + 1] = '\0';
            buf2[i] = '\0';
        }
    }
};

int main(int argc, const char *argv[])
{
    // insert code here...
    printf("server started\n");
    OFT_init();
    int i, listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    listenfd = open_listenfd(argv[1]);
    sbuf_init(&sbuf, SBUFSIZE);                                // line:conc:pre:initsbuf
    for (i = 0; i < NTHREADS; i++) /* Create worker threads */ // line:conc:pre:begincreate
        pthread_create(&tid, NULL, thread, NULL);              // line:conc:pre:endcreate

    while (1)
    {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        if (fork() == 0)
        {

            close(listenfd); /* Child closes its listening socket */

            echo(connfd); /* Child services client */

            close(connfd); /* Child closes connection with client */

            exit(0); /* Child exits */
        }
        close(connfd); /* Parent closes connected socket (important!) */
    }
}