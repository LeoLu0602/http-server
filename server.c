#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#define BACKLOG 10 // maximum number of pending connections in the queue
#define BUF_SIZE 4096 // 4 KB

void parseHttpReq(char* s, char parsed[3][BUF_SIZE]);
void* handleClient(void* arg);

int main(int argc, char* argv[]) {
  // check usage
  if (argc != 2) {
    perror("usage: ./server <port>\n");
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
    int clientFd;

    if ((clientFd = accept(serverFd, (struct sockaddr*)&clientAddr, &clientAddrLen)) == -1) {
      perror("accept failed");
      continue;
    }

    // create a new thread to handle client request
    pthread_t thread;

    if (pthread_create(&thread, NULL, handleClient, (void*)&clientFd)) {
      perror("thread creation failed");
      exit(EXIT_FAILURE);
    }

    if (pthread_detach(thread)) {
      perror("thread detach failed");
      exit(EXIT_FAILURE);
    }
  }

  close(serverFd);

  return 0;
}


// assume valid http request
void parseHttpReq(char* s, char parsed[3][BUF_SIZE]) {
  printf("%s", s);

  // get first line
  char firstLine[BUF_SIZE];
  char* newLinePos = strchr(s, '\n');
  size_t len = newLinePos - s;
  
  strncpy(firstLine, s, len);
  firstLine[len] = '\0';
  printf("first line: %s\n", firstLine);

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
  
  strcpy(parsed[row], start);
}

void* handleClient(void* arg) {
  int bytesRecv;
  int clientFd = *(int*)arg;
  char buf[BUF_SIZE];
  char parsed[3][BUF_SIZE]; // 3 rows: method, path, and version

  if ((bytesRecv = recv(clientFd, buf, sizeof(buf), 0)) == -1) {
    perror("recv failed");
    exit(EXIT_FAILURE);
  }

  parseHttpReq(buf, parsed);
  printf("method: %s\npath: %s\nversion: %s\n", parsed[0], parsed[1], parsed[2]);
}
