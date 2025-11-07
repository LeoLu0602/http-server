# HTTP Server

A minimal multithreaded **HTTP/1.1 server** in C using a **thread pool** for concurrent request handling.

## Features

-   **Thread Pool:** Fixed-size worker threads handle multiple client requests concurrently using a synchronized task queue. This improves efficiency and prevents excessive thread creation for each new connection.

-   **Security:** Protects against directory traversal attacks (e.g., `../` in URLs), validates request paths, and ensures only files within the current directory are served, keeping the server’s file system secure.

-   **Static File Serving:** Delivers static content such as HTML, CSS, JavaScript, images, audio, and video files. The server maps the requested URL to a file on disk and sends it with the appropriate MIME type.

-   **HTTP/1.1 Support:** Supports `GET` requests and returns proper HTTP status codes:
    -   **200 OK:** Request succeeded and the file is returned.
    -   **403 Forbidden:** Access denied (e.g., restricted file or directory).
    -   **404 Not Found:** The requested file does not exist.
    -   **413 Content Too Large:** The requested resource exceeds the allowed size limit.
    -   **415 Unsupported Media Type:** The file type is not supported by the server.
    -   **501 Not Implemented:** The requested HTTP method is not supported (only `GET` is implemented).
    -   **505 HTTP Version Not Supported:** The server does not support the HTTP version used in the request.

## Performance

Benchmark using [`wrk`](https://github.com/wg/wrk):

```
wrk -t4 -c100 -d30s http://localhost:8080/index.html
```

| Configuration           | Requests/sec | Transfer/sec | Avg Latency | Notes                         |
| ----------------------- | ------------ | ------------ | ----------- | ----------------------------- |
| **With Thread Pool**    | 34,073       | 10.69 MB/s   | 3.11 ms     | Efficient concurrent handling |
| **Without Thread Pool** | 9,017        | 2.83 MB/s    | 10.13 ms    | Sequential, slower under load |

✅ **Result:** The thread pool version achieves **~3.8× higher throughput** and significantly lower latency under high concurrency.

## Run

```
make
./server <port>
```

## Demo

<img src="demo.gif" width="600">
