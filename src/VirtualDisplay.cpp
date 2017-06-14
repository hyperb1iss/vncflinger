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

#define LOG_TAG "VNC-VirtualDisplay"
//#define LOG_NDEBUG 0
#include <utils/Log.h>

#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>

#include <gui/SurfaceComposerClient.h>

#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <GLES2/gl2ext.h>

#include <ui/Rect.h>

#include "VirtualDisplay.h"

using namespace android;

static const int kGlBytesPerPixel = 4;      // GL_RGBA


/*
 * Returns "true" if the device is rotated 90 degrees.
 */
bool VirtualDisplay::isDeviceRotated(int orientation) {
    return orientation != DISPLAY_ORIENTATION_0 &&
            orientation != DISPLAY_ORIENTATION_180;
}

/*
 * Sets the display projection, based on the display dimensions, video size,
 * and device orientation.
 */
status_t VirtualDisplay::setDisplayProjection(const sp<IBinder>& dpy,
        const DisplayInfo& mMainDpyInfo) {
    status_t err;

    // Set the region of the layer stack we're interested in, which in our
    // case is "all of it".  If the app is rotated (so that the width of the
    // app is based on the height of the display), reverse width/height.
    bool deviceRotated = isDeviceRotated(mMainDpyInfo.orientation);
    uint32_t sourceWidth, sourceHeight;
    if (!deviceRotated) {
        sourceWidth = mMainDpyInfo.w;
        sourceHeight = mMainDpyInfo.h;
    } else {
        ALOGV("using rotated width/height");
        sourceHeight = mMainDpyInfo.w;
        sourceWidth = mMainDpyInfo.h;
    }
    Rect layerStackRect(sourceWidth, sourceHeight);

    // We need to preserve the aspect ratio of the display.
    float displayAspect = (float) sourceHeight / (float) sourceWidth;


    // Set the way we map the output onto the display surface (which will
    // be e.g. 1280x720 for a 720p video).  The rect is interpreted
    // post-rotation, so if the display is rotated 90 degrees we need to
    // "pre-rotate" it by flipping width/height, so that the orientation
    // adjustment changes it back.
    //
    // We might want to encode a portrait display as landscape to use more
    // of the screen real estate.  (If players respect a 90-degree rotation
    // hint, we can essentially get a 720x1280 video instead of 1280x720.)
    // In that case, we swap the configured video width/height and then
    // supply a rotation value to the display projection.
    uint32_t videoWidth, videoHeight;
    uint32_t outWidth, outHeight;
   if (!mRotate) {
        videoWidth = mWidth;
        videoHeight = mHeight;
    } else {
        videoWidth = mHeight;
        videoHeight = mWidth;
    }
    if (videoHeight > (uint32_t)(videoWidth * displayAspect)) {
        // limited by narrow width; reduce height
        outWidth = videoWidth;
        outHeight = (uint32_t)(videoWidth * displayAspect);
    } else {
        // limited by short height; restrict width
        outHeight = videoHeight;
        outWidth = (uint32_t)(videoHeight / displayAspect);
    }
    uint32_t offX, offY;
    offX = (videoWidth - outWidth) / 2;
    offY = (videoHeight - outHeight) / 2;
    Rect displayRect(offX, offY, offX + outWidth, offY + outHeight);

    if (mRotate) {
        ALOGV("Rotated content area is %ux%u at offset x=%d y=%d\n",
                outHeight, outWidth, offY, offX);
    } else {
        ALOGV("Content area is %ux%u at offset x=%d y=%d\n",
                outWidth, outHeight, offX, offY);
    }

    SurfaceComposerClient::setDisplayProjection(dpy,
            mRotate ? DISPLAY_ORIENTATION_90 : DISPLAY_ORIENTATION_0,
            layerStackRect, displayRect);
    return NO_ERROR;
}

status_t VirtualDisplay::start(const DisplayInfo& mainDpyInfo) {

    Mutex::Autolock _l(mMutex);

    ALOGV("Orientation: %d", mainDpyInfo.orientation);
    mRotate = isDeviceRotated(mainDpyInfo.orientation);
    mWidth = mRotate ? mainDpyInfo.h : mainDpyInfo.w;
    mHeight = mRotate ? mainDpyInfo.w : mainDpyInfo.h;

    sp<ProcessState> self = ProcessState::self();
    self->startThreadPool();

    run("vnc-virtualdisplay");
    
    mState = INIT;
    while (mState == INIT) {
        mStartCond.wait(mMutex);
    }
  
    if (mThreadResult != NO_ERROR) {
        ALOGE("Failed to start VDS thread: err=%d", mThreadResult);
        return mThreadResult;
    }
    assert(mState == RUNNING);
  
    mDpy = SurfaceComposerClient::createDisplay(
            String8("VNCFlinger"), false /*secure*/);

    SurfaceComposerClient::openGlobalTransaction();
    SurfaceComposerClient::setDisplaySurface(mDpy, mProducer);
    setDisplayProjection(mDpy, mainDpyInfo);
    SurfaceComposerClient::setDisplayLayerStack(mDpy, 0);    // default stack
    SurfaceComposerClient::closeGlobalTransaction();

    ALOGV("VirtualDisplay::start successful");
    return NO_ERROR;
}

status_t VirtualDisplay::stop() {
    Mutex::Autolock _l(mMutex);
    mState = STOPPING;
    mEventCond.signal();
    return NO_ERROR;
}
  
bool VirtualDisplay::threadLoop() {
    Mutex::Autolock _l(mMutex);

    mThreadResult = setup_l();

    if (mThreadResult != NO_ERROR) {
        ALOGW("Aborting VDS thread");
        mState = STOPPED;
        release_l();
        mStartCond.broadcast();
        return false;
    }

    ALOGV("VDS thread running");
    mState = RUNNING;
    mStartCond.broadcast();

    while (mState == RUNNING) {
        mEventCond.wait(mMutex);
        ALOGD("Awake, frame available");
        processFrame_l();
    }

    ALOGV("VDS thread stopping");
    release_l();
    mState = STOPPED;
    return false;       // stop
}

status_t VirtualDisplay::setup_l() {
    status_t err;

    sp<IGraphicBufferConsumer> consumer;
    BufferQueue::createBufferQueue(&mProducer, &consumer);
    mCpuConsumer = new CpuConsumer(consumer, 1);
    mCpuConsumer->setName(String8("vds-to-cpu"));
    mCpuConsumer->setDefaultBufferSize(mWidth, mHeight);
    mProducer->setMaxDequeuedBufferCount(4);

    mCpuConsumer->setFrameAvailableListener(this);

    ALOGD("VirtualDisplay::setup_l OK");
    return NO_ERROR;
}

void* VirtualDisplay::processFrame_l() {
    ALOGD("processFrame_l\n");

    mUpdateMutex->lock();

    CpuConsumer::LockedBuffer imgBuffer;
    status_t res = mCpuConsumer->lockNextBuffer(&imgBuffer);
    if (res != OK) {
        ALOGE("Failed to lock next buffer: %s (%d)", strerror(-res), res);
        return nullptr;
    }

    ALOGV("imgBuffer ptr: %p format: %x (%dx%d, stride=%d)", imgBuffer.data, imgBuffer.format, imgBuffer.width, imgBuffer.height, imgBuffer.stride);

    void* vncbuf = mVNCScreen->frameBuffer;
    void* imgbuf = imgBuffer.data;

    for (size_t y = 0; y < mHeight; y++) {
        memcpy(vncbuf, imgbuf, mWidth * 4);
        vncbuf = (void *)((char *)vncbuf + mWidth * 4);
        imgbuf = (void *)((char *)imgbuf + imgBuffer.stride * 4);
    }
    ALOGD("buf copied");
    
    mVNCScreen->frameBuffer = (char *)imgBuffer.data;
    mVNCScreen->paddedWidthInBytes = imgBuffer.stride * 4;
    rfbMarkRectAsModified(mVNCScreen, 0, 0, mWidth, mHeight);


    mCpuConsumer->unlockBuffer(imgBuffer);
    mUpdateMutex->unlock();

    return nullptr;
}

void VirtualDisplay::release_l() {
    ALOGD("release_l");
    mCpuConsumer.clear();
    mProducer.clear();
    SurfaceComposerClient::destroyDisplay(mDpy);
}

// Callback; executes on arbitrary thread.
void VirtualDisplay::onFrameAvailable(const BufferItem& item) {
    Mutex::Autolock _l(mMutex);
    mEventCond.signal();
    ALOGD("mTimestamp=%ld mFrameNumber=%ld", item.mTimestamp, item.mFrameNumber);
}
