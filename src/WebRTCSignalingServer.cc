#include "WebRTCSignalingServer.h"
#include "log.h"
#include <iostream>
#include <random>
#include <sstream>

using json = nlohmann::json;

WebRTCSignalingServer::WebRTCSignalingServer(int port) 
    : m_port(port), m_clientIdCounter(0), m_running(true) {
    
    m_transcoderManager = std::make_shared<SharedTranscoderManager>();
    if (!m_transcoderManager->start()) {
        throw std::runtime_error("Failed to start transcoder manager");
    }
    
    rtc::WebSocketServer::Configuration config;
    config.port = port;
    config.enableTls = false;
    
    m_server = std::make_unique<rtc::WebSocketServer>(config);
    
    m_server->onClient([this](std::shared_ptr<rtc::WebSocket> client) {
        uint64_t clientId = getNextClientId();
        
        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            m_clients[clientId] = client;
        }
        
        LOG(NOTICE, "Client #%llu connected", clientId);
        
        createPeerConnectionForClient(client, clientId);
        
        client->onMessage([this, client, clientId](auto data) {
            handleMessage(client, clientId, data);
        });
        
        client->onClosed([this, clientId]() {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            std::lock_guard<std::mutex> pcLock(m_pcMutex);
            
            auto it = m_clientSubscriptions.find(clientId);
            if (it != m_clientSubscriptions.end()) {
                m_transcoderManager->unsubscribe(it->second);
                m_clientSubscriptions.erase(it);
            }
            
            auto pcIt = m_clientPCs.find(clientId);
            if (pcIt != m_clientPCs.end()) {
                if (pcIt->second->pc) {
                    pcIt->second->pc->close();
                }
                m_clientPCs.erase(pcIt);
            }
            
            m_clients.erase(clientId);
            
            {
                std::lock_guard<std::mutex> idLock(m_idMutex);
                m_availableIds.push(clientId);
            }
            
            LOG(NOTICE, "Client #%llu disconnected (Remaining: %zu)", clientId, m_clients.size());
        });
        
        client->onError([clientId](std::string error) {
            LOG(ERROR, "Client #%llu error: %s", clientId, error.c_str());
        });
    });
    
    LOG(NOTICE, "========================================");
    LOG(NOTICE, "WebRTC Server Started on port %d", port);
    LOG(NOTICE, "WebSocket URL: ws://0.0.0.0:%d", port);
    LOG(NOTICE, "Multi-client support: Yes");
    LOG(NOTICE, "========================================");
}

WebRTCSignalingServer::~WebRTCSignalingServer() {
    m_running = false;
    if (m_transcoderManager) {
        m_transcoderManager->stop();
    }
}

void WebRTCSignalingServer::run() {
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

uint64_t WebRTCSignalingServer::getNextClientId() {
    std::lock_guard<std::mutex> lock(m_idMutex);
    
    if (!m_availableIds.empty()) {
        uint64_t id = m_availableIds.front();
        m_availableIds.pop();
        LOG(DEBUG, "Reusing client ID: %llu", id);
        return id;
    }
    
    uint64_t newId = ++m_clientIdCounter;
    LOG(DEBUG, "Allocating new client ID: %llu", newId);
    return newId;
}

void WebRTCSignalingServer::createPeerConnectionForClient(std::shared_ptr<rtc::WebSocket> client, uint64_t clientId) {
    auto pc = std::make_shared<rtc::PeerConnection>();
    auto ctx = std::make_shared<ClientContext>();
    ctx->pc = pc;
    ctx->connectTime = std::chrono::steady_clock::now();
    
    pc->onStateChange([this, clientId](rtc::PeerConnection::State state) {
        const char* stateStr = "Unknown";
        switch(state) {
            case rtc::PeerConnection::State::New: stateStr = "New"; break;
            case rtc::PeerConnection::State::Connecting: stateStr = "Connecting"; break;
            case rtc::PeerConnection::State::Connected: stateStr = "Connected"; break;
            case rtc::PeerConnection::State::Disconnected: stateStr = "Disconnected"; break;
            case rtc::PeerConnection::State::Failed: stateStr = "Failed"; break;
            case rtc::PeerConnection::State::Closed: stateStr = "Closed"; break;
        }
        LOG(INFO, "Client #%llu PC State: %s", clientId, stateStr);
    });
    
    pc->onGatheringStateChange([this, client, clientId, pc, ctx](rtc::PeerConnection::GatheringState state) {
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            auto description = pc->localDescription();
            if (description && !ctx->offerSent) {
                json message;
                message["type"] = "offer";
                message["sdp"] = std::string(*description);
                
                try {
                    client->send(message.dump());
                    ctx->offerSent = true;
                    LOG(INFO, "Offer sent to client #%llu", clientId);
                } catch (const std::exception& e) {
                    LOG(ERROR, "Failed to send offer: %s", e.what());
                }
            }
        }
    });
    
    pc->onLocalCandidate([this, client, clientId](rtc::Candidate candidate) {
        if (!candidate.candidate().empty()) {
            json iceMsg;
            iceMsg["type"] = "ice";
            iceMsg["candidate"] = candidate.candidate();
            iceMsg["sdpMid"] = candidate.mid();
            
            try {
                client->send(iceMsg.dump());
                LOG(DEBUG, "ICE candidate sent to client #%llu", clientId);
            } catch(const std::exception& e) {}
        }
    });
    
    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    media.addH264Codec(96, "profile-level-id=42e01f;packetization-mode=1");
    auto videoTrack = pc->addTrack(media);
    ctx->videoTrack = videoTrack;
    
    auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
        std::random_device{}(), "video", 96, 90000, 0);
    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
        rtc::H264RtpPacketizer::Separator::StartSequence, rtpConfig);
    videoTrack->setMediaHandler(packetizer);
    
    videoTrack->onOpen([this, clientId, ctx, videoTrack]() {
        if (ctx->trackOpen) return;
        ctx->trackOpen = true;
        
        LOG(INFO, "Video track opened for client #%llu", clientId);
        
        ctx->transcoderSubId = m_transcoderManager->subscribe(
            [clientId, videoTrack, ctx](std::vector<uint8_t> &&data) {
                if (videoTrack && videoTrack->isOpen() && !data.empty()) {
                    try {
                        rtc::binary bin(data.size());
                        for (size_t i = 0; i < data.size(); i++) {
                            bin[i] = static_cast<std::byte>(data[i]);
                        }
                        videoTrack->send(bin);
                        ctx->frameCount++;
                        ctx->bytesSent += data.size();
                    } catch (const std::exception& e) {
                        // 静默失败
                    }
                }
            }
        );
        
        {
            std::lock_guard<std::mutex> lock(m_pcMutex);
            m_clientSubscriptions[clientId] = ctx->transcoderSubId;
        }
    });
    
    videoTrack->onClosed([this, clientId, ctx]() {
        if (ctx->transcoderSubId != 0) {
            m_transcoderManager->unsubscribe(ctx->transcoderSubId);
            ctx->transcoderSubId = 0;
            LOG(INFO, "Client #%llu unsubscribed", clientId);
        }
        ctx->trackOpen = false;
    });
    
    pc->setLocalDescription();
    
    {
        std::lock_guard<std::mutex> lock(m_pcMutex);
        m_clientPCs[clientId] = ctx;
    }
}

void WebRTCSignalingServer::handleMessage(std::shared_ptr<rtc::WebSocket> client, uint64_t clientId, 
                       const std::variant<rtc::binary, std::string>& data) {
    try {
        std::string messageStr;
        if (std::holds_alternative<std::string>(data)) {
            messageStr = std::get<std::string>(data);
        } else {
            auto& binary = std::get<rtc::binary>(data);
            messageStr.assign(reinterpret_cast<const char*>(binary.data()), binary.size());
        }
        
        auto j = json::parse(messageStr);
        std::string type = j.value("type", "");
        
        if (type == "answer") {
            std::lock_guard<std::mutex> lock(m_pcMutex);
            auto it = m_clientPCs.find(clientId);
            if (it != m_clientPCs.end() && it->second->pc) {
                rtc::Description answer(j["sdp"].get<std::string>(), "answer");
                it->second->pc->setRemoteDescription(answer);
                it->second->remoteDescriptionSet = true;
                
                LOG(INFO, "Answer received from client #%llu", clientId);
                
                if (!it->second->pendingIceCandidates.empty()) {
                    LOG(INFO, "Processing %zu cached ICE candidates", it->second->pendingIceCandidates.size());
                    for (size_t i = 0; i < it->second->pendingIceCandidates.size(); i++) {
                        rtc::Candidate candidate;
                        if (i < it->second->pendingIceSdpMids.size() && !it->second->pendingIceSdpMids[i].empty()) {
                            candidate = rtc::Candidate(it->second->pendingIceCandidates[i], 
                                                        it->second->pendingIceSdpMids[i]);
                        } else {
                            candidate = rtc::Candidate(it->second->pendingIceCandidates[i]);
                        }
                        it->second->pc->addRemoteCandidate(candidate);
                    }
                    it->second->pendingIceCandidates.clear();
                    it->second->pendingIceSdpMids.clear();
                }
            }
        }
        else if (type == "ice") {
            std::lock_guard<std::mutex> lock(m_pcMutex);
            auto it = m_clientPCs.find(clientId);
            if (it != m_clientPCs.end() && it->second->pc && j.contains("candidate")) {
                std::string candidateStr = j["candidate"].get<std::string>();
                std::string sdpMid = j.value("sdpMid", "");
                
                if (it->second->remoteDescriptionSet) {
                    rtc::Candidate candidate;
                    if (!sdpMid.empty()) {
                        candidate = rtc::Candidate(candidateStr, sdpMid);
                    } else {
                        candidate = rtc::Candidate(candidateStr);
                    }
                    it->second->pc->addRemoteCandidate(candidate);
                    LOG(DEBUG, "ICE candidate added for client #%llu", clientId);
                } else {
                    it->second->pendingIceCandidates.push_back(candidateStr);
                    it->second->pendingIceSdpMids.push_back(sdpMid);
                    LOG(DEBUG, "ICE candidate cached for client #%llu (total: %zu)", 
                        clientId, it->second->pendingIceCandidates.size());
                }
            }
        }
    } catch (const std::exception& e) {
        LOG(ERROR, "Error handling message: %s", e.what());
    }
}