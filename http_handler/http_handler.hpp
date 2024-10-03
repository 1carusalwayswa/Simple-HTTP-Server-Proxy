#include <string>
#include <sstream>
#include <regex>

class HttpHandler {
public:
    HttpHandler() = default;

    void SetHttpHandler(std::string msg_, int port_ = 80) {
        ori_msg = msg = msg_;
        port = port_;
        handler_type = "";
        // Judge type
        std::istringstream iss(msg);
        std::string tmp;
        iss >> tmp;

        // Maybe some bugs in furture, because there could be more http types method!!!!
        if (tmp == "GET" || tmp == "POST") {
            ParseRequest(msg);
            handler_type = "request";
        } else if (tmp.find("HTTP") != std::string::npos) {
            ParseResponse(msg);
            handler_type = "response";
        }
    }

    std::string& GetHost() {
        return host;
    }

    std::string& GetPath() {
        return path;
    }

    std::string& GetMethod() {
        return method;
    }

    std::string& GetHttpVersion() {
        return http_version;
    }

    std::string& GetStatusCode() {
        return status_code;
    }

    std::string& GetStatusPhrase() {
        return status_phrase;
    }

    std::string& GetContentLength() {
        return content_length;
    }

    std::string& GetContentType() {
        return content_type;
    }

    std::string& GetBody() {
        // std::cout << "-----------------\n";
        // std::cout << body;
        // std::cout << "!!!!!!!!!!!!!!!!!\n";
        return body;
    }

    const std::string& GetOriMsg() {
        return ori_msg;
    }

    std::string& GetHandlerType() {
        return handler_type;
    }

    std::string& GetHttpConnection() {
        return http_connection;
    }

    int& GetPort() {
        return port;
    }

    std::string GetRequest() {
        if (handler_type == "request") {
            std::string tmp_str = method + " " + path + " " + http_version;
            msg = ReplaceFirstLine(msg, tmp_str);
            msg = ReplaceField(msg, "Host", host);
            return msg;
        } else {
            return "";
        }
    }

    std::string GetResponse() {
        if (handler_type == "response") {
            msg = ReplaceField(ori_msg, "Content-Length", content_length);
            msg = ReplaceField(msg, "Content-Type", content_type);
            msg = ReplaceField(msg, "Connection", http_connection);
            msg = ReplaceField(msg, "body", body);
            return msg;
        } else {
            return "";
        }
    }

    void ResetMsg() {
        msg = ori_msg;
    }

private:
    // original msg
    std::string ori_msg;
    std::string msg;

    // common fields
    std::string http_version;
    std::string http_connection;

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
    bool is_encoding;

    std::string handler_type;
    int port;

    std::string ReplaceFirstLine(const std::string& msg, const std::string& new_first_line) {
        size_t pos = msg.find("\r\n");
        if (pos != std::string::npos) {
            return new_first_line + msg.substr(pos);
        } else {
            return new_first_line;
        }
    }

    std::string ReplaceField(const std::string& http_request, const std::string& field_name, const std::string& new_value) {
        std::regex field_regex(field_name + ":\\s*[^\\r\\n]*");
        std::string replacement = field_name + ": " + new_value;
        return std::regex_replace(http_request, field_regex, replacement);
    }

    void ParseRequest(std::string msg) {
       // Parse the msg
        std::istringstream iss(msg);
        std::string line;
        std::string tmp;

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
            } else if (line.find("Content-Type:") != std::string::npos) {
                std::istringstream iss_line(line);
                iss_line >> tmp >> content_type;
                return; // If there has more fields need to be find ,remove return.
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
            if (line.find("Connection:") != std::string::npos) {
                std::istringstream iss_line(line);
                iss_line >> tmp >> http_connection;
            } else if (line.find("Content-Endcoding") != std::string::npos) {
                is_encoding = true;
            } else if (line.find("Content-Length:") != std::string::npos) {
                std::istringstream iss_line(line);
                iss_line >> tmp >> content_length;
            } else if (line.find("Content-Type:") != std::string::npos) {
                std::istringstream iss_line(line);
                iss_line >> tmp >> content_type;
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

