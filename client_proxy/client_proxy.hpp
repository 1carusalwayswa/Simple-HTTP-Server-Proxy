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


constexpr int MAX_LEN = 4096;
constexpr int TIMEOUT = 300;

namespace ClientProxyUtils {
    struct SocketInfo {
        int sockfd;
        std::shared_ptr<std::mutex> own_socket_mutex;
    };

     struct Node {
        int client_socket;
        std::string res;
    };
}
namespace SharedBlockingQueue {
    BlockingQueue<ClientProxyUtils::Node> blocking_que;
}

class ClientProxy {
private:
    inline static std::unordered_map<std::string, ClientProxyUtils::SocketInfo> host_map;
    inline static std::mutex host_map_mutex_;

    std::shared_ptr<std::mutex> own_socket_mutex;

    // store the socket which send these HTTP request.
    int client_socket;
    int port;

public:
    int sockfd;
    struct sockaddr_in serverAddr; //serverAddr: 用于保存服务器地址的 sockaddr_in 结构体
    bool reuse_flag; // a flag to target this socket is reusable or not.

    
    HttpHandler request_handler;
    HttpHandler response_handler;

    // result
    std::string result_response; 
    
    //构造函数（含socket创建）
    ClientProxy(std::string& http_request_test, int client_socket, int port = 80) : client_socket{client_socket}, port{port} {
        reuse_flag = false;

        //调用httpHandler将原始报文解析后获得主机地址和端口号
        request_handler.SetHttpHandler(http_request_test, port);
        // std::cout << "-------------------" << std::endl;
        // std::cout << "Path: " << request_handler.GetPath() << std::endl;
        // std::cout << "-------------------" << std::endl;
        mix_request();
        
        std::string& server_host = request_handler.GetHost();
        int serverPort = request_handler.GetPort();
        // std::cout << "-------------------" << std::endl;
        // std::cout << "server_host: " << server_host << std::endl;
        // std::cout << "serverPort: " << serverPort << std::endl;
        // std::cout << "-------------------" << std::endl;
        
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
                sockfd = it->second.sockfd; //从 host_map 中获取与 server_host 对应的文件描述符，也就是连接这个server_host的sockfd信息
                own_socket_mutex = it->second.own_socket_mutex; // get the mutex for this socket from host_map
            } 
            else {
            //如果 it 是 host_map.end()，说明在host_map里就没找到server_host，执行下面的代码
                //创建socket
                sockfd = socket(AF_INET, SOCK_STREAM, 0);
                if (sockfd < 0) {
                    std::cerr << "Error creating socket!" << std::endl;
                    return ;
                }
                //下面这段代码的作用是为新的服务器主机（server_host）分配一个套接字sockfd，并为该套接字创建一个独立的互斥锁，确保后续对该套接字的操作是线程安全的。
                ClientProxyUtils::SocketInfo socket_info;
                socket_info.sockfd = sockfd;
                socket_info.own_socket_mutex = std::make_shared<std::mutex>();
                own_socket_mutex = socket_info.own_socket_mutex;
                // Add new socket info to host_map
                host_map[server_host] = socket_info;
                
            }
        }
        
        //这段代码的功能是：
        // 1. 初始化 serverAddr，设置通信的协议族（IPv4）和端口号
        // 2. 使用 getaddrinfo 解析主机名 server_host，获取服务器的 IP 地址
        // 3. 将解析得到的 IP 地址填充到 serverAddr.sin_addr 中，最终可以用这个结构来建立连接。
        
        memset(&serverAddr, (int)0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);            // 将端口号转换为网络字节序
        
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET; // 仅获取 IPv4 地址
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(server_host.c_str(), nullptr, &hints, &res) != 0) {
            std::cerr << "Error resolving hostname!" << std::endl;
            std::cerr << "Host: " << server_host << std::endl;
            return;
        }

        serverAddr.sin_addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr;
        freeaddrinfo(res);

        // if this socket has already conneted, dont need to connect again.
        if (!reuse_flag && !connectToServer()) {
            std::cerr << "[ClientProxy]: "
                      << "Host: "
                      << server_host
                      << " Failed to connect to server."
                      << std::endl;
            return;
        }

        // If you want to print some useful information, do like this.
        // std::cout << "\n[ClientProxy]: "
        //           << "Server Host: " << server_host
        //         //   << "Server Port: " << serverPort
        //           << std::endl;
    }

    // destructor
    // dont need to close socket here, because it will be closed in recvResponse function.
    // if you close it here, it will cause the recvResponse function to fail.
    ~ClientProxy() = default;

    void eraseSocketInfoInHostMap(HttpHandler request_handler) {
        std::lock_guard<std::mutex> lock_guard_(host_map_mutex_);
        host_map.erase(request_handler.GetHost());
    }
    
    // 尝试连接到服务器
    bool connectToServer() {
        std::lock_guard<std::mutex> lock_guard_(*own_socket_mutex);
        if (connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            std::cerr << "[ClientProxy]: " << "Connection to server failed! Error: " << strerror(errno) << " (" << errno << ")" << std::endl;
            eraseSocketInfoInHostMap(request_handler);
            return false;
        }
        else{
            std::cout << "[ClientProxy]: " << "Connected to server successfully!" << std::endl;
            return true;
        }
    }

    // 发送HTTP请求
    void sendRequest() {
        std::lock_guard<std::mutex> lock_guard_(*own_socket_mutex);
        std::string request = request_handler.GetRequest();
        if (send(sockfd, request.c_str(), request.length(), 0) == -1) {
            close(sockfd);
            eraseSocketInfoInHostMap(request_handler);
            std::cout << "Failed to send request";
            return;
        }
        if (!reuse_flag) {
            // if this socket is reusable
            // that means this is a thread to recv response
            // we dont need to new a thread to recv response
            std::thread(&ClientProxy::recvResponse, this, sockfd, client_socket, request_handler).detach();
        }
        // DEBUG
        std::cout << "[Socket " << sockfd << " send:] "
                  << request
                  << std::endl;
    }

    void mix_request(){
        std::string& server_path = request_handler.GetPath();
        std::string& server_host = request_handler.GetHost();

        // judge whether the request include a image request or not
        if (request_handler.GetPath().find(".jpg") != std::string::npos || request_handler.GetPath().find(".png") != std::string::npos) {
            if (request_handler.GetPath().find("smiley.") != std::string::npos) {
                server_path = "http://zebroid.ida.liu.se/fakenews/trolly.jpg";
                server_host = "zebroid.ida.liu.se";
            }
        }
    }

    // Mix the response
    // call by reference, modify the original script by invoke this function in member fucntion "receive()"
   #include <regex>
#include <string>

// Mix the response
// call by reference, modify the original script by invoke this function in member function "receive()"
    void mix_response(std::string& receivedData) {
        // 使用正则表达式匹配 img 标签，不区分大小写
        std::string result;

        // 替换 img 标签之外的 Stockholm，不区分大小写
        std::regex html_tag_regex(R"(<[^>]*>)");
        std::string::const_iterator start = result.cbegin();
        std::string::const_iterator end = result.cend();
        std::string final_result;
        std::smatch tag_match;
        
        while (start != end) {
            if (std::regex_search(start, end, tag_match, html_tag_regex)) {
                final_result.append(start, tag_match[0].first); // 添加标签之前的内容
                std::string text_to_replace(start, tag_match[0].first);
                text_to_replace = std::regex_replace(text_to_replace, std::regex("stockholm", std::regex_constants::icase), "Linköping");
                final_result.append(text_to_replace); // 添加替换后的文本
                final_result.append(tag_match[0].first, tag_match[0].second); // 添加标签本身
                start = tag_match[0].second; // 更新搜索起点
            } else {
                final_result.append(start, end); // 添加剩余的内容
                break;
            }
        }

        receivedData = final_result;
    }

    // Receive HTTP response from server
    void recvResponse(int sockfd, int client_socket, HttpHandler request_handler) {
        char buffer[MAX_LEN];
        std::string recv_msg;
        int bytes_received;

        while (true) {
            // It's too difficult to implement a perfect recv function.
            // So I just implement a simple one which cannot handle all the situation.
            // like chunked encoding.
            memset(buffer, 0, sizeof(buffer));
            bytes_received = recv(sockfd, buffer, sizeof(buffer), 0);
            if (bytes_received <= 0) {
                if (bytes_received == 0) {
                    std::cerr << "[ClientProxy]: "
                              << " Host: "
                              << request_handler.GetHost()
                              << " Connection closed."
                              << std::endl;
                } else {
                    std::cerr << "[ClientProxy]: "
                              << " Host: "
                              << request_handler.GetHost()
                              << " Failed to receive data."
                              << std::endl;
                }
                close(sockfd);
                eraseSocketInfoInHostMap(request_handler);
                return;
            }
            recv_msg += std::string(buffer, bytes_received);
            // use while rather than if
            // because there may be multiple responses in one recv
            // we need to check one by one
            while (recv_msg.find("\r\n\r\n") != std::string::npos) {
                size_t head_end = recv_msg.find("\r\n\r\n") + 4;
                std::string headers = recv_msg.substr(0, head_end);
                HttpHandler response_handler;
                response_handler.SetHttpHandler(headers);
                std::string body = recv_msg.substr(head_end);

                // check content-length
                size_t content_length_pos = headers.find("Content-Length: ");
                int content_length = 0;
                if (content_length_pos != std::string::npos) {
                    size_t content_length_end = headers.find("\r\n", content_length_pos);
                    // 16 is the length of "Content-Length: "
                    std::string content_length_str = headers.substr(content_length_pos + 16, content_length_end - content_length_pos - 16);
                    // stoi: string to int
                    content_length = std::stoi(content_length_str);
 
                    // recv_msg isn't enough, need to keep recv util body.size() == content_length
                    if (body.size() < content_length) {
                        recv_msg.erase(head_end, body.size());
                        while (body.size() < content_length) {
                            memset(buffer, 0, sizeof(buffer));
                            bytes_received = recv(sockfd, buffer, sizeof(buffer), 0);
                            if (bytes_received < 0 || (bytes_received == 0 && response_handler.GetStatusCode() != "304")) {
                                if (bytes_received == 0) {
                                    std::cerr << "[ClientProxy]: "
                                            << " Host: "
                                            << request_handler.GetHost()
                                            << " Connection closed."
                                            << std::endl;
                                } else {
                                    std::cerr << "[ClientProxy]: "
                                            << " Host: "
                                            << request_handler.GetHost()
                                            << " Failed to receive data."
                                            << std::endl;
                                }
                                close(sockfd);
                                eraseSocketInfoInHostMap(request_handler);
                                return;
                            }
                            body += std::string(buffer, bytes_received);
                        }
                        recv_msg += body;
                        // body is enough, just get the content_length data.
                        body = body.substr(0, content_length);
                    }
                } else {
                    // if there is no content-length, just recv until the connection is closed.
                    while ((bytes_received = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
                        body += std::string(buffer, bytes_received);
                    }
                }
                recv_msg = recv_msg.substr(head_end + content_length);
                // if (body.find("Stock") != std::string::npos) {
                //     std::cout << "---------------------\n";
                //     std::cout << "Body: " << body << std::endl;
                //     std::cout << "---------------------\n";
                // }

                if (response_handler.GetContentType() == "text/html") {
                    mix_response(body);
                }
                // finnaly, we get the response, just store the response in BQ.
                SharedBlockingQueue::blocking_que.push((ClientProxyUtils::Node){client_socket, headers + body}); 
                // std::cout << "[Socket " << client_socket << " recv:] "
                          //<< headers
                        //   << "length: " << headers.size() << " " << body.size() << '\n'
                        //   << "cal all length: " << head_end + content_length << '\n'
                        //   << "all lenght: " << (headers + body).size() << '\n'
                        //   << "\n---------------------\n"
                        //   << body.substr(0, 1024)
                        //   << "\n---------------------\n"
                        //   << (headers + body).substr(0, 1024)
                        //   << "\n---------------------"
                        //   << std::endl;
            }
            // Dont need to sleep, recv is a blocking function.
            // std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        // !-TODO-!
        // How to transmit multiple responses to the server_proxy?
        // I think we can use a blocking queue to store the response.
        // And the server_proxy can get the response from the queue.
        // I will try to implement it later.
        // 2024-09-26
        // Done in 2024-10-01
    }

    // Be careful for data consistency, sendHttp and receive must do in one step
    // never try to run them sperately.
    // Because if we try to run sendHttp first
    // we hold the lock, so that time only we can send
    // But if we unhold the lock, than other thread hold the lock and try to recv
    // it will recv response which sends to this thread.
    // I know it looks so stupid now, I will try to make a more elegant implementing later.XD
    void run() {
        sendRequest();
    }
    
    // Run this function after run() to get the response
    std::string response() {
        return result_response;
    }
};
