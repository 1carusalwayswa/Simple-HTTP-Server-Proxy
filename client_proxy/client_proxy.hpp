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

#include <thread>
#include <chrono>
#include <regex>

class client_proxy {
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
    
    //构造函数（含socket创建）
    client_proxy(std::string& http_request_test, int port){
        reuse_flag = false;

        //调用httpHandler将原始报文解析后获得主机地址和端口号
        request_handler.SetHttpHandler(http_request_test, port);
        std::string server_host = request_handler.GetHost();
        int serverPort = request_handler.GetPort();
        
        // braces to control lock_guard's life time and scope.
        {
            // first, judge host is already exist or not
            // If it has been created, don't need to create new socket to save RTT
            
            // 在多线程环境下安全地操作两个共享数据结构 host_map 和 socket_mutex_map。具体流程如下：
            // 1. 锁住 host_map_mutex_，安全地查找 server_host。
            // 2. 如果找到了对应的 sockfd，设置重用标志并获取套接字文件描述符。
            // 3. 在内部代码块中再次锁住 socket_mutex_map_mutex_，从 socket_mutex_map 中获取与 sockfd 相关的互斥锁，确保后续对 sockfd 的操作也是线程安全的。

            //std::lock_guard 是一种用于 RAII（资源获取即初始化）风格的互斥锁管理方式。它在构造时会自动锁住传递的互斥锁 host_map_mutex_，在作用域结束时（即代码块结束或异常抛出时），会自动解锁。
            //这里的 host_map_mutex_ 是一个互斥锁，用来保护对 host_map（主机映射表）的访问。这样就可以避免多个线程同时修改或读取 host_map 导致数据不一致的问题。
            std::lock_guard<std::mutex> lock_guard_(host_map_mutex_);

            //这行代码在 host_map 中查找名为 server_host 的键，并返回一个迭代器 it。如果找到了该键，迭代器将指向该元素；如果没找到，迭代器将等于 host_map.end()
            auto it = host_map.find(server_host);
            
            //如果 it 不是 host_map.end()，说明找到了 server_host，执行下面的代码
            if (it != host_map.end()) { 
                
                reuse_flag = true; // 表示找到了已经存在的 server_host，可以重用之前的连接
                sockfd = it->second; //从 host_map 中获取与 server_host 对应的文件描述符，也就是连接这个server_host的sockfd信息
                {
                    //这部分代码在一个新的作用域内定义了另一个std::lock_guard，锁住了 socket_mutex_map_mutex_，用来保护对 socket_mutex_map（套接字互斥锁映射表）的访问，防止其他线程同时修改该数据结构。
                    std::lock_guard<std::mutex> lock(socket_mutex_map_mutex_);
                    //通过sockfd 从 socket_mutex_map 中获取与当前套接字对应的互斥锁，并将其赋值给 own_socket_mutex，用于保护与这个sockfd相关的其他操作。
                    own_socket_mutex = socket_mutex_map[sockfd];
                }
            } 


            //如果 it 是 host_map.end()，说明在host_map里就没找到server_host，执行下面的代码
            else {
                //创建socket
                sockfd = socket(AF_INET, SOCK_STREAM, 0);
                if (sockfd < 0) {
                    std::cerr << "Error creating socket!" << std::endl;
                    return ;
                }
                //下面这段代码的作用是为新的服务器主机（server_host）分配一个套接字sockfd，并为该套接字创建一个独立的互斥锁，确保后续对该套接字的操作是线程安全的。
                host_map[server_host] = sockfd;
                // make a new mutex for this new socket
                {
                    std::lock_guard<std::mutex> lock(socket_mutex_map_mutex_);
                    socket_mutex_map[sockfd] = std::make_unique<std::mutex>();
                    own_socket_mutex = socket_mutex_map[sockfd];
                }
            }
        }
        
        //这段代码的功能是：
        // 1. 初始化 serverAddr，设置通信的协议族（IPv4）和端口号
        // 2. 使用 getaddrinfo 解析主机名 server_host，获取服务器的 IP 地址
        // 3. 将解析得到的 IP 地址填充到 serverAddr.sin_addr 中，最终可以用这个结构来建立连接。
        
        (&serverAddr, (int)0, sizeof(serverAddr));
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
     
        std::string temp = request_handler.GetPath();
        //  // Fake news in terms of picture，检查路径中是否包含 "/fakenews/smiley.jpg"
        if (temp.find("/fakenews/smiley.jpg") != std::string::npos) {
            std::string picPath = "/sites/default/files/styles/400wide/public/images/2018-2018-03/andrei-gurtov.jpg";
            std::string picHost = "www.comsoc.org";
            std::string request = "GET " + picPath + " HTTP/1.1\r\nHost: " + picHost + "\r\nConnection: close\r\n\r\n";
        }
        else{
            std::string request = "GET " + request_handler.GetPath() + " HTTP/1.1\r\nHost: " + request_handler.GetHost() + "\r\nConnection: close\r\n\r\n";
                if (send(sockfd, request.c_str(), request.length(), 0) == -1) {
                close(sockfd);
                std::cout << "Failed to send request";
            }
        }
        // DEBUG
        // std::cout << "[Socket " << sockfd << " send:] "
        //           << request
        //           << std::endl;
    }

    //文本文档按要求替换，被recv()调用
    std::string Replace() {
        char buffer[4096];
        memset(buffer, 0, sizeof(buffer));
        int bytesReceived = ::recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived < 0) {
            std::cerr << "Error receiving data!" << std::endl;
            return "";
        }

        std::string receivedData(buffer, bytesReceived);

        //Fake news in terms of text: 替换所有 "Stockholm" 为 "Linköping"
        receivedData = std::regex_replace(receivedData, std::regex("Stockholm"), "Linköping");

        return receivedData;
    }

    //持续接收数据，替换并输出
    void recv() {
        while (true) {
            std::string data = Replace();

            if (!data.empty()) {
                std::cout << "[Received and Replaced]: " << data << std::endl;
            }

            // 睡眠一会儿，模拟持续的接收操作
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // 连接关闭或超时
            if (data.empty()) {
                std::cout << "No more data to receive" << std::endl;
                break;
            }
        }
    }


    // 启动recv作为一个线程
    void startRecvThread() {
        std::thread recvThread(&client_proxy::recv, this);
        recvThread.detach();  //分离线程，允许它在后台运行
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
        recv();
    }
    
};
