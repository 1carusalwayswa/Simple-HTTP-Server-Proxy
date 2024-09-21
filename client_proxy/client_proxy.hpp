#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <unordered_map>
#include <mutex>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>  

#include "../http_handler/http_handler.hpp"
#include "../blocking_queue/Blocking_queue.hpp"
class HttpClient {
private:
    inline static std::unordered_map<std::string, int> host_map;
    inline static std::unordered_map<int, std::shared_ptr<std::mutex>> socket_mutex_map;
    inline static std::mutex host_map_mutex_;
    inline static std::mutex socket_mutex_map_mutex_;

    std::shared_ptr<std::mutex> own_socket_mutex;

public:
    int sockfd;
    struct sockaddr_in serverAddr; //serverAddr: 用于保存服务器地址的 sockaddr_in 结构体
    bool reuse_flag; // a flag to target this socket is reusable or not.
    
    HttpHandler request_handler;
    HttpHandler response_handler; 
    
    //构造函数
    HttpClient(std::string& http_request_test, int port){
        reuse_flag = false;

        //调用httpHandler将原始报文解析后获得主机地址和端口号
        request_handler.SetHttpHandler(http_request_test, port);
        std::string server_host = request_handler.GetHost();
        int serverPort = request_handler.GetPort();
        // braces to control lock_guard's life time and scope.
        {
            // first, judge host is already exist or not
            // If it has been created, don't need to create new socket to save RTT
            std::lock_guard<std::mutex> lock_guard_(host_map_mutex_);
            auto it = host_map.find(server_host);
            if (it != host_map.end()) {
                reuse_flag = true;
                sockfd = it->second;
                {
                    std::lock_guard<std::mutex> lock(socket_mutex_map_mutex_);
                    own_socket_mutex = socket_mutex_map[sockfd];
                }
            } else {
                //创建socket
                sockfd = socket(AF_INET, SOCK_STREAM, 0);
                if (sockfd < 0) {
                    std::cerr << "Error creating socket!" << std::endl;
                    return ;
                }
                host_map[server_host] = sockfd;
                // make a new mutex for this new socket
                {
                    std::lock_guard<std::mutex> lock(socket_mutex_map_mutex_);
                    socket_mutex_map[sockfd] = std::make_unique<std::mutex>();
                    own_socket_mutex = socket_mutex_map[sockfd];
                }
            }
        }
        
        //配置服务器地址
        memset(&serverAddr, (int)0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);            // 将端口号转换为网络字节序
        
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET; // 仅获取 IPv4 地址
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(server_host.c_str(), nullptr, &hints, &res) != 0) {
            std::cerr << "Error resolving hostname!" << std::endl;
            return;
        }

        serverAddr.sin_addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr;
        freeaddrinfo(res);

        // if this socket has already conneted, dont need to connect again.
        if (!reuse_flag && !connectToServer()) {
            return;
        }

        // If you want to print some useful information, do like this.
        std::cout << "\n[ClientProxy]: "
                  << "Server Host: " << server_host
                //   << "Server Port: " << serverPort
                  << std::endl;
    }

    // 尝试连接到服务器
    bool connectToServer() {
        std::lock_guard<std::mutex> lock_guard_(*own_socket_mutex);
        if (connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            std::cerr << "Connection to server failed! Error: " << strerror(errno) << " (" << errno << ")" << std::endl;
            return false;
        }
        else{
            std::cout << "Connected to server successfully!" << std::endl;
            return true;
        }
    }

    // 发送HTTP请求
    void sendHttp(){
        std::string request = "GET " + request_handler.GetPath() + " HTTP/1.1\r\nHost: " + request_handler.GetHost() + "\r\nConnection: close\r\n\r\n";
        if (send(sockfd, request.c_str(), request.length(), 0) == -1) {
            close(sockfd);
            std::cout << "Failed to send request";
        }
        // DEBUG
        // std::cout << "[Socket " << sockfd << " send:] "
        //           << request
        //           << std::endl;
    }

    // 接收数据
    std::string receive() {
        char buffer[4096];
        memset(buffer, 0, sizeof(buffer));
        int bytesReceived = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived < 0) {
            std::cerr << "Error receiving data!" << std::endl;
            return "";
        }
        // DEBUG
        // std::cout << "[Socket " << sockfd << " receive:] "
        //           << std::string(buffer, bytesReceived)
        //           << std::endl;
        return std::string(buffer, bytesReceived);
    }  

    // Be careful for data consistency, sendHttp and receive must do in one step
    // never try to run them sperately.
    // Because if we try to run sendHttp first
    // we hold the lock, so that time only we can send
    // But if we unhold the lock, than other thread hold the lock and try to recv
    // it will recv response which sends to this thread.
    // I know it looks so stupid now, I will try to make a more elegant implementing later.XD
    void run() {
        std::lock_guard<std::mutex> lock_guard_(*own_socket_mutex);
        sendHttp();
        receive();
    } 
    
   
    // std::string Get(const std::string& host, const std::string& path) {
    //     struct addrinfo hints, *res;
    //     int sockfd;
    //     // 设置 hints
    //     memset(&hints, 0, sizeof hints);
    //     hints.ai_family = AF_UNSPEC;
    //     hints.ai_socktype = SOCK_STREAM;
    //     // 获取地址信息
    //     if (getaddrinfo(host.c_str(), "80", &hints, &res) != 0) {
    //     return "Failed to get address info";
    // }


  

    // 创建 socket
    // sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    // if (sockfd == -1) {
    // freeaddrinfo(res);
    // return "Failed to create socket";
    // }

    // 连接到服务器
    // if (connect(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
    //     close(sockfd);
    //     freeaddrinfo(res);
    //     return "Failed to connect to server";
    // }

    // freeaddrinfo(res);

    // // 接收响应
    // char buffer[4096];
    // std::string response;
    // ssize_t bytes_received;
    // while ((bytes_received = recv(sockfd, buffer, sizeof buffer, 0)) > 0) {
    // response.append(buffer, bytes_received);
    // }

    // if (bytes_received == -1) {
    //     close(sockfd);
    //     return "Failed to receive response";
    // }
    
};