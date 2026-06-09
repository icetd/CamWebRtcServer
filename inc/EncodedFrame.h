#ifndef ENCODED_FRAME_H
#define ENCODED_FRAME_H

#include <vector>
#include <cstdint>

struct EncodedFrame {
    std::vector<uint8_t> data;
    uint64_t timestamp;
    uint32_t frameId;

    EncodedFrame();
    EncodedFrame(std::vector<uint8_t> &&d, uint64_t ts, uint32_t fid);
};

#endif