#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <regex.h>

#define BACKLOG 10 // maximum number of pending connections in the queue (man listen for more info)
#define BUF_SZ 4096 // 4 KB

void parseHttpReq(char* s, char* method, char* path, char* version);
void* handleClient(void* arg);
void buildHttpRes(char* method, char* path, char* version, char* res);

int main(int argc, char* argv[]) {
  // check usage
  if (argc != 2) {
    printf("usage: ./server <port>\n");
    exit(EXIT_FAILURE);
  }

  int port = atoi(argv[1]);

  // create server socket
  
  /* AF_INET: IPv4 Internet protocols
   * SOCK_STREAM: TCP
   * 0: select the default protocol for the given domain and type
  */
  int serverFd;
  struct sockaddr_in serverAddr; // sockaddr_in: IPv4
  
  if ((serverFd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    printf("socket creation failed\n");
    exit(EXIT_FAILURE);
  }
  
  // config server socket
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(port); // endianness
  serverAddr.sin_addr.s_addr = INADDR_ANY; // listen for connections from anywhere

  // bind socket to port
  if (bind(serverFd, (const struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
    printf("bind failed\n");
    exit(EXIT_FAILURE);
  }

  // listen for connections
  if (listen(serverFd, BACKLOG) == -1) {
    printf("listen failed\n");
    exit(EXIT_FAILURE);
  }

  printf("server listening on port %d\n", port);

  // handle connections
  while (1) {
    // accept client connection
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    int* pClientFd = malloc(sizeof(int));

    /*
     * Why int* pClientFd and not clientFd?
     *
     * If a new connection comes in before the thread reads it, clientFd may change. 
    */

    if ((*pClientFd = accept(serverFd, (struct sockaddr*)&clientAddr, &clientAddrLen)) == -1) {
      printf("accept failed\n");
      continue;
    }

    // create a new thread to handle client request
    pthread_t thread;

    // NULL: default attributes
    if (pthread_create(&thread, NULL, handleClient, (void*)pClientFd)) {
      printf("thread creation failed\n");
      continue;
    }
    
    // when a detached thread terminates, its resources are automatically released
    if (pthread_detach(thread)) {
      printf("thread detach failed\n");
      continue;
    }
  }

  close(serverFd);

  return 0;
}

void* handleClient(void* arg) {
  int bytesRecv;
  int clientFd = *(int*)arg;
  char buf[BUF_SZ];
  char method[BUF_SZ];
  char path[BUF_SZ];
  char version[BUF_SZ];
  char res[BUF_SZ]; // stores http response

  // flags 0: no special options
  if ((bytesRecv = recv(clientFd, buf, sizeof(buf), 0)) == -1) {
    printf("recv failed\n");
    pthread_exit(NULL);
  }

  buf[bytesRecv] = '\0'; // recv doesn't automatically null-terminate
  printf("\nHTTP request:\n\n%s\n", buf);
  parseHttpReq(buf, method, path, version);
  buildHttpRes(method, path, version, res);
  printf("\nHTTP response:\n\n%s\n", res);

  size_t bytesSent = 0;
  size_t resLen = strlen(res);

  while (bytesSent < resLen) {
    /* ssize_t send(int sockfd, const void *buf, size_t len, int flags);
     * bytes actually sent may be < len
     * flags 0: default behavior
    */
    ssize_t sent = send(clientFd, res + bytesSent, resLen - bytesSent, 0);

    if (sent == -1) {
      printf("send failed\n");
      pthread_exit(NULL);
    }

    bytesSent += sent;
  }
 
  free(arg);
  close(clientFd);
  
  return NULL;
}

void parseHttpReq(char* s, char* method, char* path, char* version) {
  // HTTP/1.1 requires \r\n (CRLF) at the end of every line
  char *pattern = "^([A-Z]+) ([^ ]+) (HTTP/[0-9.]+)\r?\n";
  regex_t regex;
  regmatch_t matches[4]; // whole + 3 groups
  
  if (regcomp(&regex, pattern, REG_EXTENDED)) {
    printf("failed to compile regex\n");
    pthread_exit(NULL);
  }

  if (regexec(&regex, s, 4, matches, 0)) {
    printf("no match found\n");
    pthread_exit(NULL);
  } else {
    strncpy(method, s + matches[1].rm_so, matches[1].rm_eo - matches[1].rm_so);
    method[matches[1].rm_eo - matches[1].rm_so] = '\0';
    strncpy(path, s + matches[2].rm_so, matches[2].rm_eo - matches[2].rm_so);
    path[matches[2].rm_eo - matches[2].rm_so] = '\0';
    strncpy(version, s + matches[3].rm_so, matches[3].rm_eo - matches[3].rm_so);
    version[matches[3].rm_eo - matches[3].rm_so] = '\0';
  }

  regfree(&regex);
}

void buildHttpRes(char* method, char* path, char* version, char* res) {
  if (strcmp(version, "HTTP/1.1")) {
    strcpy(res, "505 HTTP Version Not Supported");
  } else if (strcmp(method, "GET")) {
    strcpy(res, "501 Not Implemented");
  } else {
    char fullPath[BUF_SZ];

    sprintf(fullPath, "public%s", path);
    printf("fullPath: %s\n", fullPath);

    char* okRes = "HTTP/1.1 200 OK\r\n"
          "Content-Type: text/html; charset=UTF-8\r\n\r\n"
          "<!DOCTYPE html>\r\n"
          "<html>\r\n"
          "<head>\r\n"
          "<title>Hello Friend</title>\r\n"
          "</head>\r\n"
          "<body>\r\n"
          "Hello Friend\r\n"
          "</body>\r\n"
          "</html>\r\n";
  
    strcpy(res, okRes);
  }
}

