#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#define BACKLOG 10 // maximum number of pending connections in the queue
#define BUF_SIZE 4096 // 4 KB

int parseHttpReq(char* s, char parsed[3][BUF_SIZE]);
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
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }
  
  // config server socket
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(port); // endianness
  serverAddr.sin_addr.s_addr = INADDR_ANY; // listen for connections from anywhere

  // bind socket to port
  if (bind(serverFd, (const struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  // listen for connections
  if (listen(serverFd, BACKLOG) == -1) {
    perror("listen failed");
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
      perror("accept failed");
      continue;
    }

    // create a new thread to handle client request
    pthread_t thread;

    if (pthread_create(&thread, NULL, handleClient, (void*)pClientFd)) {
      perror("thread creation failed");
      continue;
    }

    if (pthread_detach(thread)) {
      perror("thread detach failed");
      continue;
    }
  }

  close(serverFd);

  return 0;
}

void* handleClient(void* arg) {
  int bytesRecv;
  int clientFd = *(int*)arg;
  char buf[BUF_SIZE];
  char parsed[3][BUF_SIZE]; // 3 rows: method, path, and version
  char res[BUF_SIZE];

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

  if (send(clientFd, res, strlen(res), 0) == -1) {
    printf("send failed\n");
    pthread_exit(NULL);
  }
 
  free(arg);
  close(clientFd);
  
  return NULL;
}

/* 
 * parsed[0]: method
 * parsed[1]: path
 * parsed[2]: version
 */
int parseHttpReq(char* s, char parsed[3][BUF_SIZE]) {
  // get first line
  char firstLine[BUF_SIZE];
  char* newLinePos; 

  // all HTTP/1.1 header lines and the status/request line must end with CRLF (\r\n)
  if (!(newLinePos = strchr(s, '\r')) || *(newLinePos + 1) != '\n') {
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
  
  strcpy(parsed[row++], start);

  return row;
}

void buildHttpRes(char* method, char* path, char* version, char* res) {
  if (strcmp(version, "HTTP/1.1")) {
    // 505 HTTP Version Not Supported
    strcpy(res, "505 HTTP Version Not Supported");
  } else if (strcmp(method, "GET")) {
    // 501 Not Implemented
    strcpy(res, "501 Not Implemented");
  } else if (strcmp(path, "/")) {
    // 404 Not Found
    strcpy(res, "404 Not Found");
  } else {
    // 200 OK
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

