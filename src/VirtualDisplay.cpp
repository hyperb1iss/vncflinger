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

#define LOG_TAG "VirtualDisplay"
#include <utils/Log.h>

#include <gui/BufferQueue.h>
#include <gui/CpuConsumer.h>
#include <gui/IGraphicBufferConsumer.h>
#include <gui/SurfaceComposerClient.h>

#include "VirtualDisplay.h"

using namespace vncflinger;

VirtualDisplay::VirtualDisplay(DisplayInfo* info, uint32_t width, uint32_t height,
                               sp<CpuConsumer::FrameAvailableListener> listener) {
    mWidth = width;
    mHeight = height;

    if (info->orientation == DISPLAY_ORIENTATION_0 || info->orientation == DISPLAY_ORIENTATION_180) {
        mSourceRect = Rect(info->w, info->h);
    } else {
        mSourceRect = Rect(info->h, info->w);
    }

    Rect displayRect = getDisplayRect();

    sp<IGraphicBufferConsumer> consumer;
    BufferQueue::createBufferQueue(&mProducer, &consumer);
    mCpuConsumer = new CpuConsumer(consumer, 1);
    mCpuConsumer->setName(String8("vds-to-cpu"));
    mCpuConsumer->setDefaultBufferSize(width, height);
    mProducer->setMaxDequeuedBufferCount(4);
    consumer->setDefaultBufferFormat(PIXEL_FORMAT_RGBX_8888);

    mCpuConsumer->setFrameAvailableListener(listener);

    mDpy = SurfaceComposerClient::createDisplay(String8("VNC-VirtualDisplay"), false /*secure*/);

    SurfaceComposerClient::openGlobalTransaction();
    SurfaceComposerClient::setDisplaySurface(mDpy, mProducer);

    SurfaceComposerClient::setDisplayProjection(mDpy, 0, mSourceRect, displayRect);
    SurfaceComposerClient::setDisplayLayerStack(mDpy, 0);  // default stack
    SurfaceComposerClient::closeGlobalTransaction();

    ALOGV("Virtual display (%ux%u [viewport=%ux%u] created", width, height, displayRect.getWidth(),
          displayRect.getHeight());
}

VirtualDisplay::~VirtualDisplay() {
    mCpuConsumer.clear();
    mProducer.clear();
    SurfaceComposerClient::destroyDisplay(mDpy);

    ALOGV("Virtual display destroyed");
}

Rect VirtualDisplay::getDisplayRect() {
    uint32_t outWidth, outHeight;
    if (mWidth > (uint32_t)((float)mWidth * aspectRatio())) {
        // limited by narrow width; reduce height
        outWidth = mWidth;
        outHeight = (uint32_t)((float)mWidth * aspectRatio());
    } else {
        // limited by short height; restrict width
        outHeight = mHeight;
        outWidth = (uint32_t)((float)mHeight / aspectRatio());
    }

    // position the desktop in the viewport while preserving
    // the source aspect ratio. we do this in case the client
    // has resized the window and to deal with orientation
    // changes set up by updateDisplayProjection
    uint32_t offX, offY;
    offX = (mWidth - outWidth) / 2;
    offY = (mHeight - outHeight) / 2;
    return Rect(offX, offY, offX + outWidth, offY + outHeight);
}
