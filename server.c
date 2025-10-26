#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <regex.h>
#include <limits.h>
#include <sys/stat.h>
#include <stdbool.h>

#define BACKLOG 10 // maximum number of pending connections in the queue (man listen for more info)
#define BUF_SZ 4096 // 4 KB
#define CONTENT_TYPE_MAX 128

void parseHttpReq(char* s, char* method, char* path, char* version);
void* handleClient(void* arg);
void buildHttpRes(char* method, char* path, char* version, char* res);
bool isFile(char* path);
unsigned long long getFileSize(char* path);
unsigned long long readFile(char* path, unsigned long long size, char* buffer);
bool isPathValid(char* path);
void getContentType(char* path, char* contentType);

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

  unsigned long long bytesSent = 0;
  unsigned long long resLen = strlen(res);

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
    sprintf(res, "HTTP/1.1 505 HTTP Version Not Supported\r\n\r\n");

    return;
  }

  if (strcmp(method, "GET")) {
    sprintf(res, "HTTP/1.1 501 Not Implemented\r\n\r\n");

    return;
  }

  char relativePath[PATH_MAX];
  char resolvedPath[PATH_MAX];
  char publicPath[PATH_MAX];
  
  sprintf(relativePath, "./public%s", path);
  printf("relativePath: %s\n", relativePath);

  if (!isPathValid(relativePath)) {
    sprintf(res, "HTTP/1.1 404 Not Found\r\n\r\n");

    return;
  }
  
  if (!realpath(relativePath, resolvedPath)) {
    printf("realpath failed\n");
    sprintf(res, "HTTP/1.1 500 Internal Server Error\r\n\r\n");

    return;
  }

  printf("resolvedPath: %s\n", resolvedPath);
  
  if (!realpath("./public", publicPath)) {
    printf("realpath failed\n");
    sprintf(res, "HTTP/1.1 500 Internal Server Error\r\n\r\n");

    return;
  }

  printf("publicPath: %s\n", publicPath);
 
  // make sure resolvedPath is inside public/
  if (strncmp(resolvedPath, publicPath, strlen(publicPath))) {
    sprintf(res, "HTTP/1.1 403 Forbidden\r\n\r\n");

    return;
  }

  unsigned long long fileSize = getFileSize(resolvedPath);
  char* buffer = (char*)malloc(fileSize);
  char contentType[CONTENT_TYPE_MAX];
  
  printf("fileSize: %llu\n", fileSize);
  getContentType(resolvedPath, contentType);
  printf("contentType: %s\n", contentType);
  
  if (readFile(resolvedPath, fileSize, buffer) != fileSize) {
    sprintf(res, "HTTP/1.1 500 Internal Server Error\r\n\r\n");

    return;
  }
  
  sprintf(
      res, 
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: %s\r\n"
      "Content-Length: %llu\r\n"
      "\r\n"
      "%s",
      contentType,
      fileSize,
      buffer
  );
  free(buffer);
}

bool isFile(char* path) {
  struct stat st;
  
  if (stat(path, &st)) {
    printf("stat failed\n");

    return false;
  }

  return S_ISREG(st.st_mode);
}

unsigned long long getFileSize(char* path) {
  struct stat st;

  if (stat(path, &st) != 0) {
    printf("stat failed\n");

    return 0;
  }

  return (unsigned long long)st.st_size;
}

unsigned long long readFile(char* path, unsigned long long size, char* buffer) {
  FILE* file = fopen(path, "rb");
  
  if (!file) {
    printf("error opening file\n");

    return -1;
  }

  unsigned long long bytesRead = fread(buffer, 1, size, file);

  fclose(file);
  
  return bytesRead;
}

bool isPathValid(char* path) {
  // test existence
  if (access(path, F_OK)) {
    return false;
  }

  // dose path lead to a file
  return isFile(path);
}

void getContentType(char* path, char* contentType) {
  char* ext;

  if (!(ext = strrchr(path, '.'))) {
    return;
  }
  
  if (strcmp(++ext, "html") == 0 || strcmp(ext, "htm") == 0) {
    strcpy(contentType, "text/html");
  } else if (strcmp(ext, "css") == 0) {
    strcpy(contentType, "text/css");
  } else if (strcmp(ext, "js") == 0) {
    strcpy(contentType, "text/javascript");
  }
}

