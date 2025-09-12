#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8080
#define BACKLOG 10 // maximum number of pending connections in the queue

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

  return 0;
}
