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
int append_header(string, const char *);
sbuf_t fdbuf;
cmaster cm;



int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    int listenfd, connfd;
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
        connfd = Accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
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
        /* Worker threads sleep here until the accept loop gives them a client. */
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

    string buf, idx;
    char object_buf[MAX_OBJECT_SIZE];
    int object_size = 0;

    /*
     * The port is part of the cache key because two local test servers can use
     * the same host/path while serving different content on different ports.
     */
    snprintf(idx, MAXLINE, "%s:%s %s", url_info.host, url_info.port, url_info.path);
    if (cache_get(&cm, idx, object_buf, &object_size)) {
        if (rio_writen(clientfd, object_buf, object_size) != object_size) {
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
        

        
        snprintf(buf, MAXLINE, "GET %s HTTP/1.0\r\n", url_info.path);
        
        

        if (rio_writen(connfd, buf, strlen(buf)) != strlen(buf) || rio_writen(connfd, header_info, strlen(header_info)) != strlen(header_info)) {
            fprintf(stderr, "Send request line and header error");
        close(connfd);
        return;
        }
    
        int respcur = 0;
        int total_size = 0;
    
        while((respcur = rio_readnb(&server_rio, buf, MAXLINE)) > 0) {
            if (rio_writen(clientfd, buf, respcur) != respcur) {
                fprintf(stderr, "Writen error\n");
                close(connfd);
                return;
            }   
            /* Responses are binary-safe: copy exactly respcur bytes. */
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
            cache_put(&cm, object_buf, idx, total_size);
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
    char* path_start = strchr(host_start, '/');
    char* port_start = strchr(host_start, ':');

    if (path_start == NULL) {
        path_start = host_start + strlen(host_start);
    }

    if (port_start == NULL || port_start > path_start) {
        char saved = *path_start;
        *path_start = '\0';
        snprintf(url_info->host, MAXLINE, "%s", host_start);
        snprintf(url_info->port, MAXLINE, "80");
        *path_start = saved;
    } else {
        *port_start = '\0';
        snprintf(url_info->host, MAXLINE, "%s", host_start);
        *port_start = ':';
        char saved = *path_start;
        *path_start = '\0';
        snprintf(url_info->port, MAXLINE, "%s", port_start + 1);
        *path_start = saved;
    }

    if (*path_start == '\0') {
        snprintf(url_info->path, MAXLINE, "/");
    } else {
        snprintf(url_info->path, MAXLINE, "%s", path_start);
    }

    return 0;
}

int parse_header(rio_t* rio, string header_info, string host) {
    string buf;
    int has_host_flag = 0;

    while (1) {
        if (rio_readlineb(rio, buf, MAXLINE) <= 0) {
            return -1;
        }

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

        if (append_header(header_info, buf) < 0) {
            return -1;
        }
    }

    if (!has_host_flag) {
        snprintf(buf, MAXLINE, "Host: %s\r\n", host);
        if (append_header(header_info, buf) < 0) {
            return -1;
        }
    }

    if (append_header(header_info, user_agent_hdr) < 0 ||
        append_header(header_info, "Connection: close\r\n") < 0 ||
        append_header(header_info, "Proxy-Connection: close\r\n") < 0 ||
        append_header(header_info, "\r\n") < 0) {
        return -1;
    }
    return 0;
}

int append_header(string header_info, const char *line) {
    size_t used = strlen(header_info);
    size_t added = strlen(line);

    if (used + added >= MAXLINE) {
        fprintf(stderr, "Request headers are too large\n");
        return -1;
    }

    memcpy(header_info + used, line, added + 1);
    return 0;
}
