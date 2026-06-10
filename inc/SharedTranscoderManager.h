#ifndef SHARED_TRANSCODER_MANAGER_H
#define SHARED_TRANSCODER_MANAGER_H

#include "MThread.h"
#include "transcoder.h"
#include "EncodedFrame.h"
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <vector>

class SharedTranscoderManager : public MThread
{
public:
    SharedTranscoderManager();
    ~SharedTranscoderManager();

    bool init();
    void run() override;
    void shutdown();

    uint64_t subscribe(std::function<void(std::vector<uint8_t> &&)> callback);
    void unsubscribe(uint64_t clientId);
    size_t getSubscriberCount();

private:
    void distributeFrames();

    std::shared_ptr<TransCoder> m_transcoder;

    std::atomic<bool> m_running;
    std::atomic<uint32_t> m_lastFrameId;

    std::mutex m_frameMutex;
    std::shared_ptr<EncodedFrame> m_latestFrame;
    std::condition_variable m_frameCV;
    bool m_frameAvailable;

    std::mutex m_subscribersMutex;
    std::unordered_map<uint64_t, std::function<void(std::vector<uint8_t> &&)>> m_subscribers;
    std::atomic<uint64_t> m_nextClientId;
};

#endif