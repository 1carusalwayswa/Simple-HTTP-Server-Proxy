#include <string>
#include <sstream>

class HttpHandler {
public:
    HttpHandler() = default;

    void SetHttpHandler(std::string msg_, int port_) {
        msg = msg_;
        port = port_;
        handler_type = "";
        // Judge type
        std::istringstream iss(msg);
        std::string tmp;
        iss >> tmp;

        // Maybe some bugs in furture, because there could be more http types method!!!!
        if (tmp == "GET") {
            ParseRequest(msg);
            handler_type = "request";
        } else if (tmp.find("HTTP") != std::string::npos) {
            ParseResponse(msg);
            handler_type = "response";
        }
    }

    std::string GetHost() {
        return host;
    }

    std::string GetPath() {
        return path;
    }

    std::string GetMethod() {
        return method;
    }

    std::string GetHttpVersion() {
        return http_version;
    }

    std::string GetStatusCode() {
        return status_code;
    }

    std::string GetStatusPhrase() {
        return status_phrase;
    }

    std::string GetContentLength() {
        return content_length;
    }

    std::string GetContentType() {
        return content_type;
    }

    std::string GetBody() {
        // std::cout << "-----------------\n";
        // std::cout << body;
        // std::cout << "!!!!!!!!!!!!!!!!!\n";
        return body;
    }

    std::string GetHandlerType() {
        return handler_type;
    }

    int GetPort() {
        return port;
    }

    void SetPort(int port_) {
        port = port_;
    }

private:
    // original msg
    std::string msg;
    std::string http_version;

    // Request
    std::string host;
    std::string path;
    std::string method;

    // Response
    std::string status_code;
    std::string status_phrase;
    std::string content_length;
    std::string content_type;
    std::string body;

    std::string handler_type;
    int port;

    void ParseRequest(std::string msg) {
       // Parse the msg
        std::istringstream iss(msg);
        std::string line;

        // Get the first line
        std::getline(iss, line);
        std::istringstream iss_line(line);
        iss_line >> method >> path >> http_version;

        // Get the host
        while (std::getline(iss, line)) {
            if (line.find("Host:") != std::string::npos) {
                std::istringstream iss_host(line);
                std::string host_line;
                iss_host >> host_line >> host;
                break;
            }
        } 
    }

    void ParseResponse(std::string msg) {
        std::istringstream iss(msg);
        std::string line;
        std::string tmp;

       // Get the first line
       std::getline(iss, line);
       std::istringstream iss_line(line);
       iss_line >> http_version >> status_code >> status_phrase;

       // Get the body
       while (std::getline(iss, line)) {
            if (line.find("Content-Length:") != std::string::npos) {
                std::istringstream iss_line(line);
                iss_line >> tmp >> content_length;
            } else if (line.find("Content-Type:" != std::string::nops)) {
                std::istringstream iss_line(line);
                iss_line >> tmp >> content_length;
            } else if (line.find("<html>") != std::string::npos) {
                body += line;
                if (line.find("</html>") != std::string::npos) { 
                        return;
                        // Everything down, just go.
                }
                body += "\n";
                while (std::getline(iss, line)) {
                    body += line;
                    if (line.find("</html>") != std::string::npos) {
                        // std::cout << "-----------------\n";
                        // std::cout << body;
                        // std::cout << "!!!!!!!!!!!!!!!!!\n";
                        return;
                        // Everything down, just go.
                    }
                    body += "\n";
                }
            }
       }
    }
};

