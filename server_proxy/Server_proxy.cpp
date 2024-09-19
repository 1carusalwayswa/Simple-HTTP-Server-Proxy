#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <thread>

#include "../blocking_queue/Blocking_queue.hpp"

constexpr int MAX_LEN = 4096;

struct Node {
    int client_socket;
    std::string res;
};

class ServerProxy {
private:
    // socket
    int server_socket;

    int port;
    std::string host;
    BlockingQueue<Node> blocking_que;

public:
    enum StatusCode {
        SUCCESS = 0,
        SOCKET_CREATION_FAILED = 1,
        BIND_FAILED = 2,
        LISTEN_FAILED = 3,
        ACCEPT_FAILED = 4,
        INVALID_ADDRESS = 5,
        CONNECT_FAILED = 6
    };

    ServerProxy(int port, std::string host) : port{port}, host{host}, server_socket{-1} {};
    
    StatusCode start() {
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == -1) {
            return StatusCode::SOCKET_CREATION_FAILED;
        }

        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);

        if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
            close(server_socket);
            return StatusCode::INVALID_ADDRESS;
        }

        if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
            close(server_socket);
            return StatusCode::BIND_FAILED;
        }

        if (listen(server_socket, 5) == -1) {
            close(server_socket);
            return StatusCode::LISTEN_FAILED;
        }

        return StatusCode::SUCCESS;
    }

    StatusCode run() {
        std::thread(&ServerProxy::handle_response, this).detach();
        while (true) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_len);
            if (client_socket == -1) {
                return StatusCode::ACCEPT_FAILED;
            }

            std::thread(&ServerProxy::handle_client,
                            this, client_socket).detach();
        }

        return StatusCode::SUCCESS;
    }

    void handle_response() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1000));
            Node res_node = blocking_que.pop();
            send(res_node.client_socket, res_node.res.c_str(), res_node.res.size(), 0);
        }
    }

    void handle_client(int client_socket) {
        char buffer[MAX_LEN];
        int bytes_received;

        while (true) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(client_socket, &read_fds);

            struct timeval timeout;
            timeout.tv_sec = 10;
            timeout.tv_usec = 0;

            int activity = select(client_socket + 1, &read_fds, nullptr, nullptr, &timeout);

            if (activity == -1) {
                std::cerr << "[HandleClient]: "
                          << "Select Error."
                          << std::endl;
                break;
            } else if (activity == 0) {
                std::cout << "[HandleClient]: "
                          << "Timeout, closing connection"
                          << std::endl;
                break;
            } else {
                // judge client_socket is in read_fds
                // can be removed, because there is only one fd: client_socket
                if (FD_ISSET(client_socket, &read_fds)) {
                    bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
                    if (bytes_received <= 0) {
                        std::cout << "[HandleClient]: "
                          << "Client disconnected"
                          << std::endl;
                        break;
                    }
                }

                // do some work
                // now just print for test
                std::cout << "Received: " << std::string(buffer, bytes_received) << std::endl;

                // call client_proxy do some work
                std::string msg = std::string(buffer, bytes_received); 
                // std::string msg = client_proxy.response();
                blocking_que.push((Node){client_socket, msg}); 
            }
        }

        close(client_socket);
    }

    ~ServerProxy() {
        if (server_socket != -1) {
            close(server_socket);
        }
    }
       
};
