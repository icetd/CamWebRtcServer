#ifndef WEBRTC_SIGNALING_SERVER_H
#define WEBRTC_SIGNALING_SERVER_H

#include "rtc/rtc.hpp"
#include "SharedTranscoderManager.h"
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <string>
#include <queue>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class WebRTCSignalingServer
{
public:
    explicit WebRTCSignalingServer(int port = 8080);
    ~WebRTCSignalingServer();

    void run();

private:
    struct ClientContext {
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::Track> videoTrack;
        uint64_t transcoderSubId = 0;
        bool offerSent = false;
        bool remoteDescriptionSet = false;
        std::chrono::steady_clock::time_point connectTime;
        std::atomic<uint64_t> frameCount{0};
        std::atomic<uint64_t> bytesSent{0};
        bool trackOpen = false;

        std::vector<std::string> pendingIceCandidates;
        std::vector<std::string> pendingIceSdpMids;
    };

    uint64_t getNextClientId(); // 新增：获取下一个可用的客户端 ID

    void createPeerConnectionForClient(std::shared_ptr<rtc::WebSocket> client, uint64_t clientId);
    void handleMessage(std::shared_ptr<rtc::WebSocket> client, uint64_t clientId,
                       const std::variant<rtc::binary, std::string> &data);

    int m_port;
    std::atomic<uint64_t> m_clientIdCounter;
    std::atomic<bool> m_running;
    std::unique_ptr<rtc::WebSocketServer> m_server;
    std::shared_ptr<SharedTranscoderManager> m_transcoderManager;

    std::unordered_map<uint64_t, std::shared_ptr<rtc::WebSocket>> m_clients;
    std::unordered_map<uint64_t, std::shared_ptr<ClientContext>> m_clientPCs;
    std::unordered_map<uint64_t, uint64_t> m_clientSubscriptions;

    std::mutex m_clientsMutex;
    std::mutex m_pcMutex;

    // ID 重用
    std::queue<uint64_t> m_availableIds; // 可重用的 ID
    std::mutex m_idMutex;
};

#endif