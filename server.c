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
#define HEAD_MAX (64 * 1024) // 64 KB
#define CONTENT_TYPE_MAX 128
#define FILE_SZ_MAX (1024 * 1024) // 1 MB
#define TASK_QUEUE_SZ 100
#define THREAD_CNT 10

typedef struct {
  void* (*fn)(void *arg);
  void* arg;
} task_t;

task_t taskQueue[TASK_QUEUE_SZ];
int queueFront = 0;
int queueRear = 0;
int queueCnt = 0;
pthread_mutex_t queueMutex;
pthread_cond_t queueNotEmpty;
pthread_cond_t queueNotFull;

void parseHttpReq(char* s, char* method, char* path, char* version);
void* handleClient(void* arg);
unsigned long long buildHttpRes(char* method, char* path, char* version, char* head, char* body);
bool isFile(char* path);
unsigned long long getFileSize(char* path);
unsigned long long readFile(char* path, unsigned long long size, char* buffer);
bool isPathValid(char* path);
void getContentType(char* path, char* contentType);
void* worker(void* arg);
void submitTask(void* (*fn)(void* arg), void* arg);

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
  pthread_mutex_init(&queueMutex, NULL);
  pthread_cond_init(&queueNotEmpty, NULL);
  pthread_cond_init(&queueNotFull, NULL);

  pthread_t thread[THREAD_CNT];

  for (int i = 0; i < THREAD_CNT; ++i) {
    pthread_create(&thread[i], NULL, worker, NULL);
  }

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

    submitTask(handleClient, (void*)pClientFd);
  }

  close(serverFd);
  pthread_mutex_destroy(&queueMutex);
  pthread_cond_destroy(&queueNotEmpty);
  pthread_cond_destroy(&queueNotFull);

  return 0;
}

void* handleClient(void* arg) {
  int bytesRecv;
  int clientFd = *(int*)arg;
  char buf[1048576]; // 1024 * 1024 -> 1 MB
  char method[16];
  char path[8192];
  char version[16];
  char head[HEAD_MAX];
  char body[FILE_SZ_MAX];

  // flags 0: no special options
  if ((bytesRecv = recv(clientFd, buf, sizeof(buf), 0)) == -1) {
    printf("recv failed\n");
    pthread_exit(NULL);
  }

  buf[bytesRecv] = '\0'; // recv doesn't automatically null-terminate
  printf("\n%s\n", buf);
  parseHttpReq(buf, method, path, version);
  
  unsigned long long contentLen = buildHttpRes(method, path, version, head, body);

  printf("\n%s\n", head);

  unsigned long long bytesSent = 0;
  unsigned long long headLen = strlen(head);

  // send head (status line + headers)
  while (bytesSent < headLen) {
    /* ssize_t send(int sockfd, const void *buf, size_t len, int flags);
     * bytes actually sent may be < len
     * flags 0: default behavior
    */
    ssize_t sent = send(clientFd, head + bytesSent, headLen - bytesSent, 0);

    if (sent == -1) {
      printf("send failed\n");
      pthread_exit(NULL);
    }

    bytesSent += sent;
  }
  
  // send body
  bytesSent = 0;

  while (bytesSent < contentLen) {
    ssize_t sent = send(clientFd, body + bytesSent, contentLen - bytesSent, 0);

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

unsigned long long buildHttpRes(char* method, char* path, char* version, char* head, char* body) {
  if (strcmp(version, "HTTP/1.1")) {
    snprintf(head, HEAD_MAX, "HTTP/1.1 505 HTTP Version Not Supported\r\n\r\n");

    return 0;
  }

  if (strcmp(method, "GET")) {
    snprintf(head, HEAD_MAX, "HTTP/1.1 501 Not Implemented\r\n\r\n");

    return 0;
  }

  char relativePath[PATH_MAX];
  char resolvedPath[PATH_MAX];
  char publicPath[PATH_MAX];
  
  sprintf(relativePath, "./public%s", path);
  printf("relativePath: %s\n", relativePath);

  if (!isPathValid(relativePath)) {
    snprintf(head, HEAD_MAX, "HTTP/1.1 404 Not Found\r\n\r\n");
    
    return 0;
  }
  
  if (!realpath(relativePath, resolvedPath)) {
    printf("realpath failed\n");
    snprintf(head, HEAD_MAX, "HTTP/1.1 500 Internal Server Error\r\n\r\n");

    return 0;
  }

  printf("resolvedPath: %s\n", resolvedPath);
  
  if (!realpath("./public", publicPath)) {
    printf("realpath failed\n");
    snprintf(head, HEAD_MAX, "HTTP/1.1 500 Internal Server Error\r\n\r\n");

    return 0;
  }

  printf("publicPath: %s\n", publicPath);
 
  // make sure resolvedPath is inside public/
  if (strncmp(resolvedPath, publicPath, strlen(publicPath))) {
    snprintf(head, HEAD_MAX, "HTTP/1.1 403 Forbidden\r\n\r\n");

    return 0;
  }

  unsigned long long fileSize = getFileSize(resolvedPath);
  char contentType[CONTENT_TYPE_MAX];
  
  printf("fileSize: %llu\n", fileSize);
  getContentType(resolvedPath, contentType);
  printf("contentType: %s\n", contentType);
  
  if (readFile(resolvedPath, fileSize, body) != fileSize) {
    snprintf(head, HEAD_MAX, "HTTP/1.1 500 Internal Server Error\r\n\r\n");

    return 0;
  }
  
  snprintf(
      head, 
      HEAD_MAX, 
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: %s\r\n"
      "Content-Length: %llu\r\n"
      "\r\n",
      contentType,
      fileSize
  );

  return fileSize;
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
  } else if (strcmp(ext, "gif") == 0) {
    strcpy(contentType, "image/gif");
  } else if (strcmp(ext, "ico") == 0) {
    strcpy(contentType, "image/vnd.microsoft.icon");
  } else if (strcmp(ext, "jpeg") == 0 || strcmp(ext, "jpg") == 0) {
    strcpy(contentType, "image/jpeg");
  } else if (strcmp(ext, "json") == 0) {
    strcpy(contentType, "application/json");
  } else if (strcmp(ext, "md") == 0) {
    strcpy(contentType, "text/markdown");
  } else if (strcmp(ext, "mp3") == 0) {
    strcpy(contentType, "audio/mpeg");
  } else if (strcmp(ext, "mp4") == 0) {
    strcpy(contentType, "video/mp4");
  } else if (strcmp(ext, "png") == 0) {
    strcpy(contentType, "image/png");
  } else if (strcmp(ext, "pdf") == 0) {
    strcpy(contentType, "application/pdf");
  } else if (strcmp(ext, "svg") == 0) {
    strcpy(contentType, "image/svg+xml");
  } else if (strcmp(ext, "txt") == 0) {
    strcpy(contentType, "text/plain");
  }
}

void* worker(void* arg) {

}

void submitTask(void* (*fn)(void* arg), void* arg) {

}

