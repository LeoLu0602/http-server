# HTTP Server

A minimal HTTP server implemented in C using a **thread pool** for concurrent request handling.

## Features

-   Fixed-size thread pool for concurrent request handling
-   Thread-safe task queue using mutex and condition variables
-   Main thread accepts and enqueues client connections
-   Worker threads process requests in parallel
-   Serves static files with MIME type detection
-   Supports HTTP/1.1 GET requests and status codes: 200, 403, 404, 415, 501, 505

## Setup

```
make
./server <port>
```

## Demo

<img src="demo.gif" width="800">
