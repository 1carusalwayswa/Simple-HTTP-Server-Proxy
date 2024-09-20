#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>  

#include "../http_handler/http_handler.hpp"
  

class HttpClient {

public:
    
    int sockfd;
    struct sockaddr_in serverAddr; //serverAddr: 用于保存服务器地址的 sockaddr_in 结构体
    
    HttpHandler request_handler;
    HttpHandler response_handler; 
    
    //构造函数
    HttpClient(std::string& http_request_test, int port){

        //调用httpHandler将原始报文解析后获得主机地址和端口号
        request_handler.SetHttpHandler(http_request_test, port);
        std::string serverIP = request_handler.GetHost();
        //int serverPort = request_handler.GetPort();

       
        //创建socket
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            std::cerr << "Error creating socket!" << std::endl;
        }
        
        //配置服务器地址
        memset(&serverAddr, (int)0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);            // 将端口号转换为网络字节序
        serverAddr.sin_addr.s_addr = inet_addr(serverIP.c_str());  // 转换IP地址



        std::cout << serverIP << std::endl;
        std::cout << serverPort << std::endl;
    }

    // 尝试连接到服务器
    bool connectToServer() {


        if (connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            std::cerr << "Connection to server failed!" << std::endl;
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
    }

    // 接收数据
    std::string receive() {
        char buffer[1024];
        memset(buffer, 0, sizeof(buffer));
        int bytesReceived = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived < 0) {
            std::cerr << "Error receiving data!" << std::endl;
            return "";
        }
        return std::string(buffer, bytesReceived);
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