#pragma once

#include "DeckLinkAPI.h"

// Receives every captured frame directly from the DeckLink capture thread.
// Implementations must return quickly and must not block (the capture thread feeds the driver).
class IVideoFrameSink
{
public:
    virtual ~IVideoFrameSink() = default;
    virtual void onVideoFrame(IDeckLinkVideoInputFrame* frame) = 0;
};
