#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#define BACKLOG 10 // maximum number of pending connections in the queue (man listen for more info)
#define BUF_SZ 4096 // 4 KB

int parseHttpReq(char* s, char parsed[3][BUF_SZ]);
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
  char parsed[3][BUF_SZ]; // 3 rows: method, path, and version
  char res[BUF_SZ]; // stores http response

  // flags 0: no special options
  if ((bytesRecv = recv(clientFd, buf, sizeof(buf), 0)) == -1) {
    printf("recv failed\n");
    pthread_exit(NULL);
  }

  buf[bytesRecv] = '\0'; // recv doesn't automatically null-terminate
  printf("\nHTTP request:\n\n%s\n", buf);
  
  if (parseHttpReq(buf, parsed) != 3) {
    printf("invalid request format\n");
    pthread_exit(NULL);
  }

  buildHttpRes(parsed[0], parsed[1], parsed[2], res);
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

/* 
 * parsed[0]: method
 * parsed[1]: path
 * parsed[2]: version
 *
 * return 3: good! found method, path, and version (3 rows filled)
 * return anything not 3: bad, some info missing 
*/
int parseHttpReq(char* s, char parsed[3][BUF_SZ]) {
  // get first line
  char firstLine[BUF_SZ];
  char* newLinePos; 

  // all HTTP/1.1 header lines and the status/request line must end with CRLF (\r\n)
  if (!(newLinePos = strstr(s, "\r\n"))) {
    return 0;
  }

  size_t len = newLinePos - s;
  
  strncpy(firstLine, s, len);
  firstLine[len] = '\0';

  // extract method, path, and version from the first line
  int row = 0;
  char* start = firstLine;
  char* cur = firstLine;

  while (*cur != '\0') {
    if (*cur == ' ') {
      *cur = '\0';
      strcpy(parsed[row++], start);
      start = cur + 1;
    }

    ++cur;
  }
 
  // don't forget the last part (ending with '\0' not ' ')!
  strcpy(parsed[row++], start);

  return row;
}

void buildHttpRes(char* method, char* path, char* version, char* res) {
  if (strcmp(version, "HTTP/1.1")) {
    strcpy(res, "505 HTTP Version Not Supported");
  } else if (strcmp(method, "GET")) {
    strcpy(res, "501 Not Implemented");
  } else if (strcmp(path, "/")) {
    strcpy(res, "404 Not Found");
  } else {
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

