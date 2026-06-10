#include "SharedTranscoderManager.h"
#include "log.h"
#include <chrono>

SharedTranscoderManager::SharedTranscoderManager() : m_running(false), m_lastFrameId(0), m_frameAvailable(false), m_nextClientId(0)
{
    m_transcoder = std::make_shared<TransCoder>();
}

SharedTranscoderManager::~SharedTranscoderManager()
{
    shutdown();
}

bool SharedTranscoderManager::init()
{
    m_transcoder->setOnEncoderDataCallback([this](std::vector<uint8_t> &&data) {
        if (!data.empty()) {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            auto frame = std::make_shared<EncodedFrame>(
                std::move(data),
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count(),
                ++m_lastFrameId);
            m_latestFrame = frame;
            m_frameAvailable = true;
            m_frameCV.notify_all();
        }
    });

    m_transcoder->start();

    LOG(INFO, "Shared Transcoder Manager initialized");
    return true;
}

void SharedTranscoderManager::run()
{
    LOG(INFO, "Shared Transcoder Manager distributor started");
    distributeFrames();
    LOG(INFO, "Shared Transcoder Manager distributor stopped");
}

void SharedTranscoderManager::shutdown()
{
    if (!m_running) return;

    LOG(INFO, "Stopping Shared Transcoder Manager...");
    m_running = false;
    m_frameCV.notify_all();

    if (m_transcoder) {
        m_transcoder->stop();
    }

    MThread::stop();

    LOG(INFO, "Shared Transcoder Manager stopped");
}

uint64_t SharedTranscoderManager::subscribe(std::function<void(std::vector<uint8_t> &&)> callback)
{
    std::lock_guard<std::mutex> lock(m_subscribersMutex);
    uint64_t clientId = ++m_nextClientId;
    m_subscribers[clientId] = std::move(callback);
    LOG(INFO, "Client subscribed (ID: %llu, Total: %zu)", clientId, m_subscribers.size());
    return clientId;
}

void SharedTranscoderManager::unsubscribe(uint64_t clientId)
{
    std::lock_guard<std::mutex> lock(m_subscribersMutex);
    auto it = m_subscribers.find(clientId);
    if (it != m_subscribers.end()) {
        m_subscribers.erase(it);
        LOG(INFO, "Client unsubscribed (ID: %llu, Remaining: %zu)", clientId, m_subscribers.size());
    }
}

size_t SharedTranscoderManager::getSubscriberCount()
{
    std::lock_guard<std::mutex> lock(m_subscribersMutex);
    return m_subscribers.size();
}

void SharedTranscoderManager::distributeFrames()
{
    m_running = true;
    int frameCount = 0;
    auto lastStatTime = std::chrono::steady_clock::now();

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

            for (auto &[clientId, callback] : m_subscribers) {
                try {
                    std::vector<uint8_t> clientData(frameData);
                    callback(std::move(clientData));
                } catch (const std::exception &e) {
                    LOG(WARN, "Failed to send to client #%llu: %s", clientId, e.what());
                    deadClients.push_back(clientId);
                }
            }

            for (auto clientId : deadClients) {
                m_subscribers.erase(clientId);
                LOG(WARN, "Removed dead client #%llu", clientId);
            }

            frameCount++;
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastStatTime).count();
            if (elapsed >= 10) {
                LOG(INFO, "Distribution stats: %d frames in %lds, %d fps, %zu subscribers",
                    frameCount, elapsed, frameCount / elapsed, m_subscribers.size());
                frameCount = 0;
                lastStatTime = now;
            }
        }
    }
}