#include "./server_proxy/server_proxy.hpp"

int main() {
    ServerProxy server_proxy;
    server_proxy.start();
    server_proxy.run();
    return 0;
}