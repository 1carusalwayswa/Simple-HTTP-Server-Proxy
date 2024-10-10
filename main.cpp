#include "./server_proxy/server_proxy.hpp"

int main() {
    // start the server proxy
    ServerProxy server_proxy;
    server_proxy.start();
    // run the server proxy
    server_proxy.run();
    return 0;
}