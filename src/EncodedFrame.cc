#include "EncodedFrame.h"

EncodedFrame::EncodedFrame() : timestamp(0), frameId(0)
{}

EncodedFrame::EncodedFrame(std::vector<uint8_t> &&d, uint64_t ts, uint32_t fid) : data(std::move(d)), timestamp(ts), frameId(fid)
{}