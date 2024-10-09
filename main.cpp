#include "./server_proxy/server_proxy.hpp"

int main() {
    // start the server proxy
    ServerProxy server_proxy;
    server_proxy.start();
    // run the server proxy
    server_proxy.run();
    return 0;
}

/*
ReadMe:
Introduction
This is a simple proxy server that can handle multiple clients' requests. The server proxy can parse the HTTP request and response. The server proxy can also handle the response from the server and send the response to the client.

ServerProxy is a proxy server that can handle multiple clients' requests.
ClientProxy is a proxy client that can send requests to the server.
BlockingQueue is a thread-safe queue that can be used to store the response from the server.
HttpHandler is a class that can parse the HTTP request and response.

How to run the server proxy
To run the server proxy, you need to compile the code and run the executable file. You can use the following commands to compile the code:
g++ -std=c++17 main.cpp -o server_proxy && ./server_proxy
After running the server proxy, you can use a web browser to send requests to the server proxy. The server proxy will handle the requests and send the responses back to the web browser.
Note: you need to configure the web browser to use the server proxy as the proxy server, the default port is 27777, the default IP is 127.0.0.1, you can change the port and IP in the code.

Test results
1. It could work well with the web browser in both school and home.
2. It could pass the all test cases which lists in the lab description.
3. Due to the time, we don't implement the handle of HTTP 1.0/1.1, it just always keep connection alive.
4. It solve the TCP stick problem by separate the request and response by "\r\n\r\n", but could only handle the normal request and response, like the chunked encoding is not supported.

*/