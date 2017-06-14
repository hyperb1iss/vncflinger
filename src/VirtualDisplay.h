/*
 * Copyright 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef VDS_H
#define VDS_H

#include "Program.h"
#include "EglWindow.h"

#include <gui/BufferQueue.h>
#include <gui/GLConsumer.h>
#include <gui/IGraphicBufferProducer.h>
#include <ui/DisplayInfo.h>
#include <utils/Thread.h>

#include <rfb/rfb.h>

#define NUM_PBO 2

namespace android {

/*
 * Support for "frames" output format.
 */
class VirtualDisplay : public GLConsumer::FrameAvailableListener, Thread {
public:
    VirtualDisplay(rfbScreenInfoPtr vncScreen) : Thread(false),
        mVNCScreen(vncScreen),
        mThreadResult(UNKNOWN_ERROR),
        mState(UNINITIALIZED),
        mIndex(0)
        {}

    // Create an "input surface", similar in purpose to a MediaCodec input
    // surface, that the virtual display can send buffers to.  Also configures
    // EGL with a pbuffer surface on the current thread.
    virtual status_t start(const DisplayInfo& mainDpyInfo);

    virtual status_t stop();

    static bool isDeviceRotated(int orientation);

private:
    VirtualDisplay(const VirtualDisplay&);
    VirtualDisplay& operator=(const VirtualDisplay&);

    // Destruction via RefBase.
    virtual ~VirtualDisplay() {
        assert(mState == UNINITIALIZED || mState == STOPPED);
    }

    virtual status_t setDisplayProjection(const sp<IBinder>& dpy,
            const DisplayInfo& mainDpyInfo);

    // (overrides GLConsumer::FrameAvailableListener method)
    virtual void onFrameAvailable(const BufferItem& item);

    // (overrides Thread method)
    virtual bool threadLoop();

    // One-time setup (essentially object construction on the overlay thread).
    status_t setup_l();

    // Release all resources held.
    void release_l();

    // Process a frame received from the virtual display.
    void* processFrame_l();

    rfbScreenInfoPtr mVNCScreen;

    uint32_t mHeight, mWidth;
    bool mRotate;

    // Used to wait for the FrameAvailableListener callback.
    Mutex mMutex;

    // Initialization gate.
    Condition mStartCond;

    // Thread status, mostly useful during startup.
    status_t mThreadResult;

    // Overlay thread state.  States advance from left to right; object may
    // not be restarted.
    enum { UNINITIALIZED, INIT, RUNNING, STOPPING, STOPPED } mState;

    // Event notification.  Overlay thread sleeps on this until a frame
    // arrives or it's time to shut down.
    Condition mEventCond;

    // Producer side of queue, passed into the virtual display.
    // The consumer end feeds into our GLConsumer.
    sp<IGraphicBufferProducer> mProducer;

    // This receives frames from the virtual display and makes them available
    // as an external texture.
    sp<GLConsumer> mGlConsumer;

    // EGL display / context / surface.
    EglWindow mEglWindow;

    // GL rendering support.
    Program mExtTexProgram;

    // External texture, updated by GLConsumer.
    GLuint mExtTextureName;

    // Pixel data buffers.
    size_t mBufSize;
    GLuint* mPBO;
    unsigned int mIndex;

    sp<IBinder> mDpy;
};

}; // namespace android

#endif /* VDS_H */
