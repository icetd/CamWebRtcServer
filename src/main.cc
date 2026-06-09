#include "rtc/rtc.hpp"
#include "transcoder.h"
#include <iostream>
#include <memory>
#include <thread>
#include <random>
#include <nlohmann/json.hpp>

using nlohmann::json;

int main() {
    rtc::InitLogger(rtc::LogLevel::Info);
    
    auto pc = std::make_shared<rtc::PeerConnection>();
    
    pc->onGatheringStateChange([pc](rtc::PeerConnection::GatheringState state) {
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            auto description = pc->localDescription();
            json message = {
                {"type", description->typeString()},
                {"sdp", std::string(description.value())}
            };
            std::cout << "\n=== OFFER ===\n" << message.dump(4) << "\n=============\n";
        }
    });

    // 创建视频轨道
    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    media.addH264Codec(96, "profile-level-id=42e01f;packetization-mode=1");
    auto videoTrack = pc->addTrack(media);
    
    // 配置 H264 打包器
    auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
        std::random_device{}(), "video", 96, 90000, 0);
    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
        rtc::H264RtpPacketizer::Separator::StartSequence, rtpConfig);
    videoTrack->setMediaHandler(packetizer);
    
    // 创建编码器
    auto transcoder = std::make_shared<TransCoder>();
    transcoder->setOnEncoderDataCallback([videoTrack](std::vector<uint8_t> &&data) {
        if (videoTrack && videoTrack->isOpen()) {
            rtc::binary bin(data.size());
            for (size_t i = 0; i < data.size(); i++) {
                bin[i] = static_cast<std::byte>(data[i]);
            }
            videoTrack->send(bin);
        }
    });
    
    videoTrack->onOpen([transcoder]() {
        std::cout << "Track opened, starting encoder\n";
        std::thread([transcoder]() { transcoder->run(); }).detach();
    });
    
    pc->setLocalDescription();
    
    // 等待浏览器 Answer
    std::cout << "Paste browser answer (JSON): ";
    std::string line;
    std::getline(std::cin, line);
    
    auto j = json::parse(line);
    rtc::Description answer(j["sdp"].get<std::string>(), j["type"].get<std::string>());
    pc->setRemoteDescription(answer);
    
    std::cout << "Streaming... Press Enter to stop\n";
    std::cin.get();
    
    return 0;
}