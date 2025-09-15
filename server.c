#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define PORT 8080
#define BACKLOG 10 // maximum number of pending connections in the queue

void* handleConnection(void* arg);

int main(int argc, char* argv[]) {
  int serverFd;
  struct sockaddr_in serverAddr; // sockaddr_in: IPv4

  // create server socket
  /* AF_INET: IPv4 Internet protocols
   * SOCK_STREAM: TCP
   * 0: select the default protocol for the given domain and type
  */
  if ((serverFd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }
  
  // config server socket
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(PORT); // endianness
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

  printf("Server listening on port %d\n", PORT);

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

    if (pthread_create(&thread, NULL, handleConnection, (void*)&clientFd)) {
      perror("thread creation failed");
      exit(EXIT_FAILURE);
    }

    if (pthread_detach(thread)) {
      perror("thread detach failed");
      exit(EXIT_FAILURE);
    }
  }

  return 0;
}

void* handleConnection(void* arg) {

}
