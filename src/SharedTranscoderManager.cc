#include "SharedTranscoderManager.h"
#include <iostream>
#include <chrono>

SharedTranscoderManager::SharedTranscoderManager() 
    : m_running(false), m_lastFrameId(0), m_frameAvailable(false), m_nextClientId(0) {
    m_transcoder = std::make_shared<TransCoder>();
}

SharedTranscoderManager::~SharedTranscoderManager() {
    stop();
}

bool SharedTranscoderManager::start() {
    if (m_running) return true;
    
    m_running = true;
    
    m_transcoder->setOnEncoderDataCallback([this](std::vector<uint8_t> &&data) {
        if (!data.empty()) {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            auto frame = std::make_shared<EncodedFrame>(
                std::move(data), 
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
                ).count(),
                ++m_lastFrameId
            );
            m_latestFrame = frame;
            m_frameAvailable = true;
            m_frameCV.notify_all();
        }
    });
    
    m_encoderThread = std::make_unique<std::thread>([this]() {
        std::cout << "Shared Transcoder started" << std::endl;
        m_transcoder->run();
        std::cout << "Shared Transcoder stopped" << std::endl;
    });
    
    m_distributorThread = std::make_unique<std::thread>([this]() {
        distributeFrames();
    });
    
    std::cout << "Shared Transcoder Manager initialized" << std::endl;
    return true;
}

void SharedTranscoderManager::stop() {
    m_running = false;
    m_frameCV.notify_all();
    if (m_encoderThread && m_encoderThread->joinable()) m_encoderThread->join();
    if (m_distributorThread && m_distributorThread->joinable()) m_distributorThread->join();
}

uint64_t SharedTranscoderManager::subscribe(std::function<void(std::vector<uint8_t>&&)> callback) {
    std::lock_guard<std::mutex> lock(m_subscribersMutex);
    uint64_t clientId = ++m_nextClientId;
    m_subscribers[clientId] = std::move(callback);
    std::cout << "Client subscribed (ID: " << clientId << ", Total: " << m_subscribers.size() << ")" << std::endl;
    return clientId;
}

void SharedTranscoderManager::unsubscribe(uint64_t clientId) {
    std::lock_guard<std::mutex> lock(m_subscribersMutex);
    auto it = m_subscribers.find(clientId);
    if (it != m_subscribers.end()) {
        m_subscribers.erase(it);
        std::cout << "Client unsubscribed (ID: " << clientId << ", Remaining: " << m_subscribers.size() << ")" << std::endl;
    }
}

size_t SharedTranscoderManager::getSubscriberCount() {
    std::lock_guard<std::mutex> lock(m_subscribersMutex);
    return m_subscribers.size();
}

void SharedTranscoderManager::distributeFrames() {
    while (m_running) {
        std::unique_lock<std::mutex> lock(m_frameMutex);
        m_frameCV.wait_for(lock, std::chrono::milliseconds(33), [this] {
            return m_frameAvailable || !m_running;
        });
        if (!m_running) break;
        
        if (m_frameAvailable && m_latestFrame && !m_latestFrame->data.empty()) {
            m_frameAvailable = false;
            std::vector<uint8_t> frameData = m_latestFrame->data;
            lock.unlock();
            
            std::lock_guard<std::mutex> subsLock(m_subscribersMutex);
            std::vector<uint64_t> deadClients;
            for (auto& [clientId, callback] : m_subscribers) {
                try {
                    std::vector<uint8_t> clientData(frameData);
                    callback(std::move(clientData));
                } catch (const std::exception& e) {
                    deadClients.push_back(clientId);
                }
            }
            for (auto clientId : deadClients) {
                m_subscribers.erase(clientId);
            }
        }
    }
}