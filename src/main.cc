#include "WebRTCSignalingServer.h"
#include <iostream>

int main() {
    rtc::InitLogger(rtc::LogLevel::Info);
    rtc::Preload();
    
    std::cout << "\n╔══════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║   WebRTC Multi-Client Server (Local Network)    ║" << std::endl;
    std::cout << "║         For LAN use - No STUN/TURN required      ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════╝\n" << std::endl;
    
    try {
        WebRTCSignalingServer server(8080);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}