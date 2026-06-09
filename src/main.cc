#include "WebRTCSignalingServer.h"
#include <iostream>
#include "INIReader.h"
#include "log.h"

int main() {
    int port;
    INIReader configs("./configs/config.ini");
    if (configs.ParseError() < 0) {
        printf("read config failed.");
        exit(1);
    } else {
        std::string level = configs.Get("log", "level", "NOTICE");
        if (level == "NOTICE") {
            initLogger(NOTICE);
        } else if (level == "INFO") {
            initLogger(INFO);
        } else if (level == "ERROR") {
            initLogger(ERROR);
        }
        port = configs.GetInteger("Server", "port", 8080);
    }

    rtc::InitLogger(rtc::LogLevel::Error);
    rtc::Preload();
    
    std::cout << "\n╔══════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║   WebRTC Multi-Client Server (Local Network)    ║" << std::endl;
    std::cout << "║         For LAN use - No STUN/TURN required      ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════╝\n" << std::endl;
    
    try {
        WebRTCSignalingServer server(port);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}