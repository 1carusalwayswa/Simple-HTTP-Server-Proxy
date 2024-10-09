#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <thread>
#include <map>

#include "../blocking_queue/Blocking_queue.hpp"
#include "../client_proxy/client_proxy.hpp"

namespace ServerProxyUtils {
    std::atomic<bool> running(true);
}


class ServerProxy {
private:
    // socket
    int server_socket;

    int port;
    std::string host;

    std::mutex socket_mutex;

    std::string ParseRequest(std::string msg) {
        // Parse the msg
        std::istringstream iss(msg);
        std::string line;

        // Get the host
        while (std::getline(iss, line)) {
            if (line.find("Host:") != std::string::npos) {
                std::istringstream iss_host(line);
                std::string host_line, host;
                iss_host >> host_line >> host;
                return host;
            }
       }

       return ""; 
    }


public:
    // status code to indicate the status of the server.
    enum StatusCode {
        SUCCESS = 0,
        SOCKET_CREATION_FAILED = 1,
        BIND_FAILED = 2,
        LISTEN_FAILED = 3,
        ACCEPT_FAILED = 4,
        INVALID_ADDRESS = 5,
        CONNECT_FAILED = 6,
        SOCKET_OPTION_FAILED = 7
    };

    ServerProxy(std::string host = "127.0.0.1", int port = 27777) : port{port}, host{host}, server_socket{-1} {};
    
    StatusCode start() {
        ServerProxyUtils::running = true;

        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == -1) {
            return StatusCode::SOCKET_CREATION_FAILED;
        }

        int opt = 1;
        if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            std::cerr << "Failed to set SO_REUSEADDR" << std::endl;
            close(server_socket);
            return StatusCode::SOCKET_CREATION_FAILED;
        }

        struct timeval timeout;
        timeout.tv_sec = TIMEOUT*10;
        timeout.tv_usec = 0;
        if (setsockopt(server_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            std::cerr << "Error setting socket options: " << strerror(errno) << std::endl;
            close(server_socket);
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
        // start the thread to handle the response.
        // all response will push into the BQ by the client_proxy.
        // keep running and try to get response from the BQ.
        // if there is a response, send it to the client.
        std::thread(&ServerProxy::handle_response, this).detach();
        while (ServerProxyUtils::running) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_len);
            if (client_socket == -1) {
                return StatusCode::ACCEPT_FAILED;
            }

            // set the new socket timeout
            // This timeout is used for the client_socket, not the server_socket.
            // They both have their own timeout.
            struct timeval timeout;
            timeout.tv_sec = TIMEOUT * 10;
            timeout.tv_usec = 0;
            if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
                std::cerr << "Failed to set receive timeout: " << strerror(errno) << std::endl;
                close(client_socket);
                return StatusCode::SOCKET_OPTION_FAILED;
            }
            if (setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
                std::cerr << "Failed to set send timeout: " << strerror(errno) << std::endl;
                close(client_socket);
                return StatusCode::SOCKET_OPTION_FAILED;
            }

            // start a new thread to handle the client request.
            // it will parse the request, and send the request to the server.
            // and get the response from the server, and push the response to the BQ.
            std::thread(&ServerProxy::handle_client,
                            this, client_socket).detach();
        }

        return StatusCode::SUCCESS;
    }

    // handle the response from the BQ.
    // get the response from the BQ, and send the response to the client.
    // ** Due to we use the blocking queue, we dont need to sleep here. **
    void handle_response() {
        while (ServerProxyUtils::running) {
            ClientProxyUtils::Node res_node = SharedBlockingQueue::blocking_que.pop();
            ssize_t byte_sent = send(res_node.client_socket, res_node.res.c_str(), res_node.res.size(), 0);
            // Debug
            // std::cout << "[Send_Response]: \n"
            //           << "Socket" << res_node.client_socket << '\n'
            //           << "length: " << res_node.res.size() << " "
            //           << res_node.res.substr(0, 512)
            //           << std::endl;
            if (byte_sent == -1) {
                std::cerr << "[ServerProxy]: " << "Socket" << res_node.client_socket 
                          << " Failed to send response" << std::endl;
                // handle the error, close the socket. 
                close(res_node.client_socket);
                return;
            }
        }
    }

    // handle the browser(client) request.
    // parse the request, and send the request to the server.
    void handle_client(int client_socket) {
        char buffer[MAX_LEN];
        int bytes_received;
        std::string recv_msg;
        std::string::size_type pos;

        while (true) {
            std::lock_guard<std::mutex> lock_guard_(socket_mutex);
            bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
            if (bytes_received <= 0) {
                if (bytes_received == 0) {
                    std::cerr << "[ServerProxy]: " << "Socket" << client_socket 
                              << " Connection closed." << std::endl
                              << strerror(errno) << std::endl;
                } else {
                    std::cerr << "[ServerProxy]: " << "Scoket" << client_socket
                              << " Failed to receive data." << std::endl
                              << strerror(errno) << std::endl;
                }
                close(client_socket);
                return;
            }

            recv_msg += std::string(buffer, bytes_received);

            // handle the received data, separate the complete request
            // to deal with the tcp stick problem.
            while ((pos = recv_msg.find("\r\n\r\n")) != std::string::npos) {
                std::string complete_request = recv_msg.substr(0, pos + 4);
                recv_msg.erase(0, pos + 4);

                // Debug
                // std::cout << "[Receive socket"<< client_socket << "]: "
                //           << complete_request.substr(0, 512)
                //           << std::endl;
                ClientProxy client_proxy(complete_request, client_socket);
                client_proxy.run();
            }
        }
    }

    void stop() {
        ServerProxyUtils::running = false;
        if (server_socket != -1) {
            close(server_socket);
        }
    }

    ~ServerProxy() {
        stop(); 
    }
       
};
