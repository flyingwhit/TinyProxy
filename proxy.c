#include <stdio.h>
#include "csapp.h"
#include "sbuf.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define NBLOCK 10
#define NTHREADS 128 
#define SBUFSIZE 1024

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

typedef char string[MAXLINE];

typedef struct url {
    string host;
    string port;
    string path;
}url_t;

void* thread(void *vargp);
void do_request(int);
void do_get(int, rio_t*, string);
int parse_url(string, url_t*);
int parse_header(rio_t*, string, string);
sbuf_t fdbuf;
cmaster cm;



int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    sbuf_init(&fdbuf, SBUFSIZE);
    cache_init(&cm);

    for (int i = 0; i < NTHREADS; i++) {
        Pthread_create(&tid, NULL, thread, NULL);
    }
    
    listenfd = Open_listenfd(argv[1]);
    while(1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, &clientaddr, &clientlen);
        sbuf_insert(&fdbuf, connfd);    
    }

    sbuf_deinit(&fdbuf);
    cache_deinit(&cm);
    close(listenfd);
    return 0;
}

void* thread(void *vargp) {
    pthread_detach(pthread_self());

    while (1) {
        int connfd = sbuf_remove(&fdbuf);
        do_request(connfd);
        close(connfd);    
    }

    return NULL;
}

void do_request(int clientfd) {
    rio_t rio;
    string buf;

    Rio_readinitb(&rio, clientfd);
    if (!Rio_readlineb(&rio, buf, MAXLINE)) {
        fprintf(stderr, "Read request line error %s", strerror(errno));
        return;
    }

    string url, method, version;
    if (sscanf(buf, "%s %s %s", method, url, version) != 3) {
        fprintf(stderr, "Parse request line error %s", strerror(errno));
        return;
    }

    if (!strcasecmp("GET", method)) {
        do_get(clientfd, &rio, url);
    }    
}
   

void do_get(int clientfd, rio_t* rio, string url) {
    url_t url_info;    

    if (parse_url(url, &url_info) < 0) {
        fprintf(stderr, "Parse url error %s", strerror(errno));
        return;
    }

    int i;
    string buf, idx;
    sprintf(idx,"%s %s", url_info.host, url_info.path);
    if ((i = cache_match(&cm, idx)) >= 0) {
        memcpy(buf, cm.mbuf[i].buf, cm.mbuf[i].size);
        if (rio_writen(clientfd, buf, cm.mbuf[i].size) != cm.mbuf[i].size) {
            fprintf(stderr, "Writen error\n");
        }
        return;
    } else {
        string header_info;
        header_info[0] = '\0';
        if (parse_header(rio, header_info, url_info.host) < 0) {
            fprintf(stderr, "Parse header error %s", strerror(errno));        
            return;
        }

        int connfd = open_clientfd(url_info.host, url_info.port);
        if (connfd < 0) {
            fprintf(stderr, "Open connect to %s error", url_info.host);
            return;
        }
        
        rio_t server_rio;
        rio_readinitb(&server_rio, connfd);
        

        
        sprintf(buf, "GET %s HTTP/1.0\r\n", url_info.path);
        
        

        if (rio_writen(connfd, buf, strlen(buf)) != strlen(buf) || rio_writen(connfd, header_info, strlen(header_info)) != strlen(header_info)) {
            fprintf(stderr, "Send request line and header error");
        close(connfd);
        return;
        }
    
        int respcur = 0;
        int total_size = 0;
        char object_buf[MAX_OBJECT_SIZE];
    
        while((respcur = rio_readnb(&server_rio, buf, MAXLINE)) > 0) {
            if (rio_writen(clientfd, buf, respcur) != respcur) {
                fprintf(stderr, "Writen error\n");
                return;
            }   
            if (total_size + respcur <= MAX_OBJECT_SIZE) {
                memcpy(object_buf + total_size, buf, respcur);
                total_size += respcur;
            }
        }

        if (respcur < 0) {
            fprintf(stderr, "Response error\n");
            close(connfd);
            return;
        }
        
        if (total_size <= MAX_OBJECT_SIZE) {
            cache_assert(&cm, object_buf, idx, total_size);
        }
        
        close(connfd);
        return;
    }
}

int parse_url(string url, url_t* url_info) {
    int http_prefix_len = strlen("http://");
    if (strncasecmp(url, "http://", http_prefix_len)) {
        fprintf(stderr, "Not http protocol\n");
        return -1;
    }

    char* host_start = url + http_prefix_len;
    char* port_start = strchr(host_start, ':');
    char* path_start = strchr(host_start, '/');

    if (path_start == NULL) {
        fprintf(stderr, "Invalid http request");
        return -1;
    }

    if (port_start == NULL) {
        *path_start = '\0';
        strcpy(url_info->host, host_start);
        strcpy(url_info->port, "80");
        *path_start = '/';
        strcpy(url_info->path, path_start);
    } else {
        *port_start = '\0';
        strcpy(url_info->host, host_start);
        *port_start = ':';
        *path_start = '\0';
        strcpy(url_info->port, port_start + 1);
        *path_start = '/';
        strcpy(url_info->path, path_start);
    }

    return 0;
}

int parse_header(rio_t* rio, string header_info, string host) {
    string buf;
    int has_host_flag = 0;

    while (1) {
        rio_readlineb(rio, buf, MAXLINE);

        if (strcmp(buf, "\r\n") == 0) {
            break;
        }    

        if (!strncasecmp(buf, "Host:", strlen("Host:"))) {
            has_host_flag = 1;
        }

        if (!strncasecmp(buf, "User-Agent:", strlen("User-Agent:"))) {
            continue;
        }

        if (!strncasecmp(buf, "Connection:", strlen("Connection:"))) {
            continue;
        }

        if (!strncasecmp(buf, "Proxy-Connection:", strlen("Proxy-Connection:"))) {
            continue;
        }

        strcat(header_info, buf);
    }

    if (!has_host_flag) {
        sprintf(buf, "Host: %s\r\n", host);
        strcat(header_info, buf);
    }

    strcat(header_info, user_agent_hdr);
    strcat(header_info, "Connection: close\r\n");
    strcat(header_info, "Proxy-Connection: close\r\n");
    strcat(header_info, "\r\n");
    return 0;
}

