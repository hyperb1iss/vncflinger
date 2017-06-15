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
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>

#include <gui/ISurfaceComposer.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/IGraphicBufferProducer.h>

#include "InputDevice.h"
#include "VNCFlinger.h"


using namespace android;

status_t VNCFlinger::start() {
    sp<ProcessState> self = ProcessState::self();
    self->startThreadPool();

    status_t err = NO_ERROR;

    mMainDpy = SurfaceComposerClient::getBuiltInDisplay(
            ISurfaceComposer::eDisplayIdMain);
    err = SurfaceComposerClient::getDisplayInfo(mMainDpy, &mMainDpyInfo);
    if (err != NO_ERROR) {
        ALOGE("Failed to get display characteristics\n");
        return err;
    }
    mHeight = mMainDpyInfo.h;
    mWidth = mMainDpyInfo.w;

    err = createVNCServer();
    if (err != NO_ERROR) {
        ALOGE("Failed to start VNCFlinger: err=%d", err);
        return err;
    }

    ALOGD("VNCFlinger is running!");

    rfbRunEventLoop(mVNCScreen, -1, true);

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
                processFrame();
                mFrameAvailable = false;
            }
        }

        destroyVirtualDisplay();
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

    mDpy = SurfaceComposerClient::createDisplay(
            String8("VNC-VirtualDisplay"), false /*secure*/);

    SurfaceComposerClient::openGlobalTransaction();
    SurfaceComposerClient::setDisplaySurface(mDpy, mProducer);
    //setDisplayProjection(mDpy, mainDpyInfo);
    SurfaceComposerClient::setDisplayLayerStack(mDpy, 0);    // default stack
    SurfaceComposerClient::closeGlobalTransaction();

    ALOGV("Virtual display created");
    return NO_ERROR;
}

status_t VNCFlinger::destroyVirtualDisplay() {
    mCpuConsumer.clear();
    mProducer.clear();
    SurfaceComposerClient::destroyDisplay(mDpy);
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
    mVNCScreen->frameBuffer = (char *) mVNCBuf;
    mVNCScreen->desktopName = "VNCFlinger";
    mVNCScreen->alwaysShared = TRUE;
    mVNCScreen->httpDir = NULL;
    mVNCScreen->port = VNC_PORT;
    mVNCScreen->newClientHook = (rfbNewClientHookPtr) VNCFlinger::onNewClient;
    mVNCScreen->kbdAddEvent = InputDevice::keyEvent;
    mVNCScreen->ptrAddEvent = InputDevice::pointerEvent;
    mVNCScreen->displayHook = (rfbDisplayHookPtr) VNCFlinger::onFrameStart;
    mVNCScreen->displayFinishedHook = (rfbDisplayFinishedHookPtr) VNCFlinger::onFrameDone;
    mVNCScreen->serverFormat.trueColour = true;
    mVNCScreen->serverFormat.bitsPerPixel = 32;
    mVNCScreen->handleEventsEagerly = true;
    mVNCScreen->deferUpdateTime = 0;
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
        InputDevice::start(mWidth, mHeight);
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
            InputDevice::stop();
            mEventCond.signal();
        }
    }

    ALOGI("Client disconnected (%zu)", mClientCount);

    return mClientCount;
}

ClientGoneHookPtr VNCFlinger::onClientGone(rfbClientPtr cl) {
    ALOGV("onClientGone");
    VNCFlinger *vf = (VNCFlinger *)cl->screen->screenData;
    vf->removeClient();
    return 0;
}

enum rfbNewClientAction VNCFlinger::onNewClient(rfbClientPtr cl) {
    ALOGV("onNewClient");
    cl->clientGoneHook = (ClientGoneHookPtr) VNCFlinger::onClientGone;
    VNCFlinger *vf = (VNCFlinger *)cl->screen->screenData;
    vf->addClient();
    return RFB_CLIENT_ACCEPT;
}

void VNCFlinger::onFrameStart(rfbClientPtr cl) {
    VNCFlinger *vf = (VNCFlinger *)cl->screen->screenData;
    vf->mUpdateMutex.lock();
    ALOGV("frame start");
}

void VNCFlinger::onFrameDone(rfbClientPtr cl, int status) {
    VNCFlinger *vf = (VNCFlinger *)cl->screen->screenData;
    vf->mUpdateMutex.unlock();
    ALOGV("frame done! %d", status);
}

void VNCFlinger::rfbLogger(const char *format, ...) {
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
    ALOGV("onFrameAvailable: mTimestamp=%ld mFrameNumber=%ld",
            item.mTimestamp, item.mFrameNumber);
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

    ALOGV("processFrame: ptr: %p format: %x (%dx%d, stride=%d)",
            imgBuffer.data, imgBuffer.format, imgBuffer.width,
            imgBuffer.height, imgBuffer.stride);

    void* vncbuf = mVNCScreen->frameBuffer;
    void* imgbuf = imgBuffer.data;

    // Copy the frame to the server's buffer
    if (imgBuffer.stride > mWidth) {
        // Image has larger stride, so we need to copy row by row
        for (size_t y = 0; y < mHeight; y++) {
            memcpy(vncbuf, imgbuf, mWidth * 4);
            vncbuf = (void *)((char *)vncbuf + mWidth * 4);
            imgbuf = (void *)((char *)imgbuf + imgBuffer.stride * 4);
        }
    } else {
        memcpy(vncbuf, imgbuf, mWidth * mHeight * 4);
    }

    rfbMarkRectAsModified(mVNCScreen, 0, 0, mWidth, mHeight);

    mCpuConsumer->unlockBuffer(imgBuffer);
}
