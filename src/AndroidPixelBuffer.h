//
// vncflinger - Copyright (C) 2017 Steve Kondik
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

#ifndef ANDROID_PIXEL_BUFFER_H
#define ANDROID_PIXEL_BUFFER_H

#include <utils/Mutex.h>
#include <utils/RefBase.h>

#include <ui/DisplayInfo.h>
#include <ui/Rect.h>

#include <rfb/PixelBuffer.h>
#include <rfb/PixelFormat.h>

using namespace android;

namespace vncflinger {

class AndroidPixelBuffer : public RefBase, public rfb::ManagedPixelBuffer {
  public:
    AndroidPixelBuffer();

    virtual void setDisplayInfo(DisplayInfo* info);

    virtual void setWindowSize(uint32_t width, uint32_t height);

    virtual ~AndroidPixelBuffer();

    class BufferDimensionsListener {
      public:
        virtual void onBufferDimensionsChanged(uint32_t width, uint32_t height) = 0;
        virtual ~BufferDimensionsListener() {
        }
    };

    void setDimensionsChangedListener(BufferDimensionsListener* listener) {
        mListener = listener;
    }

    bool isRotated() {
        return mRotated;
    }

    Rect getSourceRect();

  private:
    static bool isDisplayRotated(uint8_t orientation);

    virtual void setBufferRotation(bool rotated);

    virtual void updateBufferSize(bool fromDisplay = false);

    Mutex mLock;

    // width/height is swapped due to display orientation
    bool mRotated;

    // preferred size of the client's window
    uint32_t mClientWidth, mClientHeight;

    // size of the display
    uint32_t mSourceWidth, mSourceHeight;

    // current ratio between server and client
    float mScaleX, mScaleY;

    // callback when buffer size changes
    BufferDimensionsListener* mListener;

    // Android virtual display is always 32-bit
    static const rfb::PixelFormat sRGBX;
};
};

#endif
