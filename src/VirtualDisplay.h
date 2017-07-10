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

#ifndef VIRTUAL_DISPLAY_H_
#define VIRTUAL_DISPLAY_H_

#include <utils/RefBase.h>

#include <gui/CpuConsumer.h>
#include <gui/IGraphicBufferProducer.h>

#include <ui/DisplayInfo.h>
#include <ui/Rect.h>

using namespace android;

namespace vncflinger {

class VirtualDisplay : public RefBase {
  public:
    VirtualDisplay(DisplayInfo* info, uint32_t width, uint32_t height,
                   sp<CpuConsumer::FrameAvailableListener> listener);

    virtual ~VirtualDisplay();

    virtual Rect getDisplayRect();

    virtual Rect getSourceRect() {
        return mSourceRect;
    }

    CpuConsumer* getConsumer() {
        return mCpuConsumer.get();
    }

  private:
    float aspectRatio() {
        return (float)mSourceRect.getHeight() / (float)mSourceRect.getWidth();
    }

    // Producer side of queue, passed into the virtual display.
    sp<IGraphicBufferProducer> mProducer;

    // This receives frames from the virtual display and makes them available
    sp<CpuConsumer> mCpuConsumer;

    // Virtual display
    sp<IBinder> mDpy;

    sp<CpuConsumer::FrameAvailableListener> mListener;

    uint32_t mWidth, mHeight;
    Rect mSourceRect;
};
};
#endif
