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

#define LOG_TAG "VNCFlinger"
#include <utils/Log.h>

#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>

#include <gui/IGraphicBufferProducer.h>
#include <gui/ISurfaceComposer.h>
#include <gui/SurfaceComposerClient.h>

#include "InputDevice.h"
#include "VNCFlinger.h"

using namespace android;

status_t VNCFlinger::start() {
    sp<ProcessState> self = ProcessState::self();
    self->startThreadPool();

    mMainDpy = SurfaceComposerClient::getBuiltInDisplay(ISurfaceComposer::eDisplayIdMain);

    updateDisplayProjection();

    status_t err = createVNCServer();
    if (err != NO_ERROR) {
        ALOGE("Failed to start VNCFlinger: err=%d", err);
        return err;
    }

    rfbRunEventLoop(mVNCScreen, -1, true);

    ALOGD("VNCFlinger is running!");

    eventLoop();

    return NO_ERROR;
}

void VNCFlinger::eventLoop() {
    mRunning = true;

    Mutex::Autolock _l(mEventMutex);
    while (mRunning) {
        mEventCond.wait(mEventMutex);

        // spurious wakeup? never.
        if (mClientCount == 0) {
            continue;
        }

        // this is a new client, so fire everything up
        status_t err = createVirtualDisplay();
        if (err != NO_ERROR) {
            ALOGE("Failed to create virtual display: err=%d", err);
        }

        // loop while clients are connected and process frames
        // on the main thread when signalled
        while (mClientCount > 0) {
            mEventCond.wait(mEventMutex);
            if (mFrameAvailable) {
                if (!updateDisplayProjection()) {
                    processFrame();
                }
                mFrameAvailable = false;
            }
        }
        Mutex::Autolock _l(mUpdateMutex);
        destroyVirtualDisplayLocked();
    }
}

status_t VNCFlinger::createVirtualDisplay() {
    sp<IGraphicBufferConsumer> consumer;
    BufferQueue::createBufferQueue(&mProducer, &consumer);
    mCpuConsumer = new CpuConsumer(consumer, 1);
    mCpuConsumer->setName(String8("vds-to-cpu"));
    mCpuConsumer->setDefaultBufferSize(mWidth, mHeight);
    mProducer->setMaxDequeuedBufferCount(4);

    mListener = new FrameListener(this);
    mCpuConsumer->setFrameAvailableListener(mListener);

    mDpy = SurfaceComposerClient::createDisplay(String8("VNC-VirtualDisplay"), false /*secure*/);

    SurfaceComposerClient::openGlobalTransaction();
    SurfaceComposerClient::setDisplaySurface(mDpy, mProducer);
    // setDisplayProjection(mDpy, mainDpyInfo);
    SurfaceComposerClient::setDisplayLayerStack(mDpy, 0);  // default stack
    SurfaceComposerClient::closeGlobalTransaction();

    mVDSActive = true;

    ALOGV("Virtual display (%dx%d) created", mWidth, mHeight);

    return NO_ERROR;
}

status_t VNCFlinger::destroyVirtualDisplayLocked() {
    mCpuConsumer.clear();
    mProducer.clear();
    SurfaceComposerClient::destroyDisplay(mDpy);

    mVDSActive = false;

    ALOGV("Virtual display destroyed");

    return NO_ERROR;
}

status_t VNCFlinger::createVNCServer() {
    status_t err = NO_ERROR;

    rfbLog = VNCFlinger::rfbLogger;
    rfbErr = VNCFlinger::rfbLogger;

    // 32-bit color
    mVNCScreen = rfbGetScreen(&mArgc, mArgv, mWidth, mHeight, 8, 3, 4);
    if (mVNCScreen == NULL) {
        ALOGE("Unable to create VNCScreen");
        return NO_INIT;
    }

    mVNCBuf = new uint8_t[mWidth * mHeight * 4];
    mVNCScreen->frameBuffer = (char*)mVNCBuf;
    mVNCScreen->desktopName = "VNCFlinger";
    mVNCScreen->alwaysShared = TRUE;
    mVNCScreen->httpDir = NULL;
    mVNCScreen->port = VNC_PORT;
    mVNCScreen->newClientHook = (rfbNewClientHookPtr)VNCFlinger::onNewClient;
    mVNCScreen->kbdAddEvent = InputDevice::onKeyEvent;
    mVNCScreen->ptrAddEvent = InputDevice::onPointerEvent;
    mVNCScreen->displayHook = (rfbDisplayHookPtr)VNCFlinger::onFrameStart;
    mVNCScreen->displayFinishedHook = (rfbDisplayFinishedHookPtr)VNCFlinger::onFrameDone;
    mVNCScreen->serverFormat.trueColour = true;
    mVNCScreen->serverFormat.bitsPerPixel = 32;
    mVNCScreen->handleEventsEagerly = true;
    mVNCScreen->deferUpdateTime = 1;
    mVNCScreen->screenData = this;
    rfbInitServer(mVNCScreen);

    /* Mark as dirty since we haven't sent any updates at all yet. */
    rfbMarkRectAsModified(mVNCScreen, 0, 0, mWidth, mHeight);

    return err;
}

status_t VNCFlinger::stop() {
    Mutex::Autolock _L(mEventMutex);

    mClientCount = 0;
    mRunning = false;

    mEventCond.signal();

    return NO_ERROR;
}

size_t VNCFlinger::addClient() {
    Mutex::Autolock _l(mEventMutex);
    if (mClientCount == 0) {
        mClientCount++;
        updateFBSize(mWidth, mHeight, mWidth);
        InputDevice::getInstance().start(mWidth, mHeight);
        mEventCond.signal();
    }

    ALOGI("Client connected (%zu)", mClientCount);

    return mClientCount;
}

size_t VNCFlinger::removeClient() {
    Mutex::Autolock _l(mEventMutex);
    if (mClientCount > 0) {
        mClientCount--;
        if (mClientCount == 0) {
            InputDevice::getInstance().stop();
            mEventCond.signal();
        }
    }

    ALOGI("Client disconnected (%zu)", mClientCount);

    return mClientCount;
}

ClientGoneHookPtr VNCFlinger::onClientGone(rfbClientPtr cl) {
    ALOGV("onClientGone");
    VNCFlinger* vf = (VNCFlinger*)cl->screen->screenData;
    vf->removeClient();
    return 0;
}

enum rfbNewClientAction VNCFlinger::onNewClient(rfbClientPtr cl) {
    ALOGV("onNewClient");
    cl->clientGoneHook = (ClientGoneHookPtr)VNCFlinger::onClientGone;
    VNCFlinger* vf = (VNCFlinger*)cl->screen->screenData;
    vf->addClient();
    return RFB_CLIENT_ACCEPT;
}

void VNCFlinger::onFrameStart(rfbClientPtr cl) {
    VNCFlinger* vf = (VNCFlinger*)cl->screen->screenData;
    vf->mUpdateMutex.lock();
    ALOGV("frame start");
}

void VNCFlinger::onFrameDone(rfbClientPtr cl, int status) {
    VNCFlinger* vf = (VNCFlinger*)cl->screen->screenData;
    vf->mUpdateMutex.unlock();
    ALOGV("frame done! %d", status);
}

void VNCFlinger::rfbLogger(const char* format, ...) {
    va_list args;
    char buf[256];

    va_start(args, format);
    vsprintf(buf, format, args);
    ALOGI("%s", buf);
    va_end(args);
}

void VNCFlinger::FrameListener::onFrameAvailable(const BufferItem& item) {
    Mutex::Autolock _l(mVNC->mEventMutex);
    mVNC->mFrameAvailable = true;
    mVNC->mEventCond.signal();
    ALOGV("onFrameAvailable: mTimestamp=%ld mFrameNumber=%ld", item.mTimestamp, item.mFrameNumber);
}

void VNCFlinger::processFrame() {
    ALOGV("processFrame\n");

    // Take the update mutex. This ensures that we don't dequeue
    // a new buffer and blow away the one being sent to a client.
    // The BufferQueue is self-regulating and will drop frames
    // automatically for us.
    Mutex::Autolock _l(mUpdateMutex);

    CpuConsumer::LockedBuffer imgBuffer;
    status_t res = mCpuConsumer->lockNextBuffer(&imgBuffer);
    if (res != OK) {
        ALOGE("Failed to lock next buffer: %s (%d)", strerror(-res), res);
        return;
    }

    ALOGV("processFrame: ptr: %p format: %x (%dx%d, stride=%d)", imgBuffer.data, imgBuffer.format,
          imgBuffer.width, imgBuffer.height, imgBuffer.stride);

    updateFBSize(imgBuffer.width, imgBuffer.height, imgBuffer.stride);

    memcpy(mVNCBuf, imgBuffer.data, imgBuffer.stride * imgBuffer.height * 4);

    rfbMarkRectAsModified(mVNCScreen, 0, 0, imgBuffer.width, imgBuffer.height);

    mCpuConsumer->unlockBuffer(imgBuffer);
}

/*
 * Returns "true" if the device is rotated 90 degrees.
 */
bool VNCFlinger::isDeviceRotated(int orientation) {
    return orientation != DISPLAY_ORIENTATION_0 && orientation != DISPLAY_ORIENTATION_180;
}

/*
 * Sets the display projection, based on the display dimensions, video size,
 * and device orientation.
 */
bool VNCFlinger::updateDisplayProjection() {
    DisplayInfo info;
    status_t err = SurfaceComposerClient::getDisplayInfo(mMainDpy, &info);
    if (err != NO_ERROR) {
        ALOGE("Failed to get display characteristics\n");
        return true;
    }

    if (info.orientation == mOrientation) {
        return false;
    }

    // Set the region of the layer stack we're interested in, which in our
    // case is "all of it".  If the app is rotated (so that the width of the
    // app is based on the height of the display), reverse width/height.
    bool deviceRotated = isDeviceRotated(info.orientation);
    int sourceWidth, sourceHeight;
    if (!deviceRotated) {
        sourceWidth = info.w;
        sourceHeight = info.h;
    } else {
        ALOGV("using rotated width/height");
        sourceHeight = info.w;
        sourceWidth = info.h;
    }

    Mutex::Autolock _l(mUpdateMutex);
    mWidth = sourceWidth;
    mHeight = sourceHeight;
    mOrientation = info.orientation;

    if (!mVDSActive) {
        return true;
    }

    destroyVirtualDisplayLocked();
    createVirtualDisplay();
    return true;
}

status_t VNCFlinger::updateFBSize(int width, int height, int stride) {
    if ((mVNCScreen->paddedWidthInBytes / 4) != stride || mVNCScreen->height != height ||
        mVNCScreen->width != width) {
        ALOGD("updateFBSize: old=[%dx%d %d] new=[%dx%d %d]", mVNCScreen->width, mVNCScreen->height,
              mVNCScreen->paddedWidthInBytes / 4, width, height, stride);

        delete[] mVNCBuf;
        mVNCBuf = new uint8_t[stride * height * 4];
        memset(mVNCBuf, 0, stride * height * 4);

        // little dance here to avoid an ugly immediate resize
        if (mVNCScreen->height != height || mVNCScreen->width != width) {
            rfbNewFramebuffer(mVNCScreen, (char*)mVNCBuf, width, height, 8, 3, 4);
        } else {
            mVNCScreen->frameBuffer = (char*)mVNCBuf;
        }
        mVNCScreen->paddedWidthInBytes = stride * 4;
    }
    return NO_ERROR;
}
