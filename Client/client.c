// yongqi liang 75181206

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

#define MAXLINE 8192 /* Max text line length */
#define MAXBUF 8192  /* Max I/O tokens[1] size */
#define LISTENQ 1024 /* Second argument to listen() */
int clientfd = 0;
int token_length = 0;

int open_clientfd(char *hostname, char *port)
{
    int clientfd;
    struct addrinfo hints, *listp, *p;

    /* Get a list of potential server addresses */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM; /* Open a connection */
    hints.ai_flags = AI_NUMERICSERV; /* ... using a numeric port arg. */
    hints.ai_flags |= AI_ADDRCONFIG; /* Recommended for connections */
    getaddrinfo(hostname, port, &hints, &listp);

    /* Walk the list for one that we can successfully connect to */
    for (p = listp; p; p = p->ai_next)
    {
        /* Create a socket descriptor */
        if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
            continue; /* Socket failed, try the next */

        /* Connect to the server */
        if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
            break; /* Success */

        close(clientfd); /* Connect failed, try another */
    }

    /* Clean up */
    freeaddrinfo(listp);
    if (!p) /* All connects failed */
        return -1;
    else /* The last connect succeeded */
        return clientfd;
}

char **split_line(char *line)
{
    token_length = 0;
    int capacity = 16;

    char **tokens = malloc(capacity * sizeof(char *));

    char *delimiters = " \t\r\n";
    char *token = strtok(line, delimiters);

    while (token != NULL)
    {
        tokens[token_length] = token;
        token_length++;
        token = strtok(NULL, delimiters);
    }
    tokens[token_length] = NULL;
    return tokens;
}


int main(int argc, const char *argv[])
{
    int i = 0;
    char *host, *port, input[MAXLINE], line[MAXLINE];
    char temp[MAXLINE];

    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        exit(0);
    }
    host = argv[1];
    port = argv[2];
    clientfd = open_clientfd(host, port);
    char *spliter = " \n";
    while (1)
    { 
        for (i = 0; i < MAXLINE; i++)
        {
            input[i] = '\0';
            line[i] = '\0';
        }
        printf("> ");
        memset(input, 0, 200);
        fgets(input, (sizeof input / sizeof input[0]), stdin);
        if (input[strlen(input) - 1] == '\n')
        {
            input[strlen(input) - 1] = 0;
        }
        strcpy(temp, input);
        char **tokens = split_line(temp);
        if (strcmp(tokens[0], "quit") == 0)
        {
            break;
        }
        if (strcmp(tokens[0], "openRead") == 0)
        {
            if (tokens[1] == NULL)
            {
                printf("Invalid Syntax!\n");
                continue;
            }
            write(clientfd, input, strlen(input));
            read(clientfd, input, MAXLINE);
            fputs(input, stdout);
            continue;
        }
        if (strcmp(tokens[0], "openAppend") == 0)
        {
            if (tokens[1] == NULL)
            {
                printf("Invalid Syntax!\n");
                continue;
            }
            write(clientfd, input, strlen(input));
            read(clientfd, input, MAXLINE);
            fputs(input, stdout);
            continue;
        }
        if (strcmp(tokens[0], "read") == 0)
        {
            if (tokens[1] == NULL)
            {
                printf("Invalid Syntax!\n");
                continue;
            }
            write(clientfd, input, strlen(input));
            read(clientfd, input, MAXLINE);
            fputs(input, stdout);
            continue;
        }
        if (strcmp(tokens[0], "append") == 0)
        {
            if (tokens[1] == NULL)
            {
                printf("Invalid Syntax!\n");
                continue;
            }
            write(clientfd, input, strlen(input));
            read(clientfd, input, MAXLINE);
            fputs(input, stdout);
            continue;
        }
        if (strcmp(tokens[0], "close") == 0)
        {
            write(clientfd, input, strlen(input));
            read(clientfd, input, MAXLINE);
            fputs(input, stdout);
            continue;
        }
        if(strcmp(tokens[0], "getHash") == 0){
            if (tokens[1] == NULL)
            {
                printf("Invalid Syntax!\n");
                continue;
            }
            write(clientfd, input, strlen(input));
            read(clientfd, input, MAXLINE);
            fputs(input, stdout);
            continue;
        }
        else
        {
            printf("Invalid Syntax!\n");
            continue;
        }
        free(tokens);
    }
    close(clientfd); 
    exit(0);
    return 0;
}