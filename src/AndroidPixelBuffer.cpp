//
// vncflinger - Copyright (C) 2021 Stefanie Kondik
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#define LOG_TAG "AndroidPixelBuffer"
#include <utils/Log.h>

#include <ui/DisplayInfo.h>

#include "AndroidPixelBuffer.h"

using namespace vncflinger;
using namespace android;

const rfb::PixelFormat AndroidPixelBuffer::sRGBX(32, 24, false, true, 255, 255, 255, 0, 8, 16);

AndroidPixelBuffer::AndroidPixelBuffer()
    : ManagedPixelBuffer(), mRotated(false), mScaleX(1.0f), mScaleY(1.0f) {
    setPF(sRGBX);
    setSize(0, 0);
}

AndroidPixelBuffer::~AndroidPixelBuffer() {
    mListener = nullptr;
}

bool AndroidPixelBuffer::isDisplayRotated(uint8_t orientation) {
    return orientation != DISPLAY_ORIENTATION_0 && orientation != DISPLAY_ORIENTATION_180;
}

void AndroidPixelBuffer::setBufferRotation(bool rotated) {
    if (rotated != mRotated) {
        ALOGV("Orientation changed, swap width/height");
        mRotated = rotated;
        setSize(height_, width_);
        std::swap(mScaleX, mScaleY);
        stride = width_;

        if (mListener != nullptr) {
            mListener->onBufferDimensionsChanged(width_, height_);
        }
    }
}

void AndroidPixelBuffer::updateBufferSize(bool fromDisplay) {
    uint32_t w = 0, h = 0;

    // if this was caused by the source size changing (doesn't really
    // happen on most Android hardware), then we need to consider
    // a previous window size set by the client
    if (fromDisplay) {
        w = (uint32_t)((float)mSourceWidth * mScaleX);
        h = (uint32_t)((float)mSourceHeight * mScaleY);
        mClientWidth = w;
        mClientHeight = h;
    } else {
        w = mClientWidth;
        h = mClientHeight;
    }

    mScaleX = (float)mClientWidth / (float)mSourceWidth;
    mScaleY = (float)mClientHeight / (float)mSourceHeight;

    if (w == (uint32_t)width_ && h == (uint32_t)height_) {
        return;
    }

    ALOGV("Buffer dimensions changed: old=(%dx%d) new=(%dx%d) scaleX=%f scaleY=%f", width_, height_,
          w, h, mScaleX, mScaleY);

    setSize(w, h);

    if (mListener != nullptr) {
        mListener->onBufferDimensionsChanged(width_, height_);
    }
}

void AndroidPixelBuffer::setWindowSize(uint32_t width, uint32_t height) {
    if (mClientWidth != width || mClientHeight != height) {
        ALOGV("Client window size changed: old=(%dx%d) new=(%dx%d)", mClientWidth, mClientHeight,
              width, height);
        mClientWidth = width;
        mClientHeight = height;
        updateBufferSize();
    }
}

void AndroidPixelBuffer::setDisplayInfo(DisplayInfo* info) {
    bool rotated = isDisplayRotated(info->orientation);
    setBufferRotation(rotated);

    uint32_t w = rotated ? info->h : info->w;
    uint32_t h = rotated ? info->w : info->h;

    if (w != mSourceWidth || h != mSourceHeight) {
        ALOGV("Display dimensions changed: old=(%dx%d) new=(%dx%d)", mSourceWidth, mSourceHeight, w,
              h);
        mSourceWidth = w;
        mSourceHeight = h;
        updateBufferSize(true);
    }
}

Rect AndroidPixelBuffer::getSourceRect() {
    return Rect(mSourceWidth, mSourceHeight);
}
