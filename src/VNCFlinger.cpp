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

VNCFlinger::VNCFlinger() {
    mOrientation = -1;
    mMainDpy = SurfaceComposerClient::getBuiltInDisplay(ISurfaceComposer::eDisplayIdMain);
    updateDisplayProjection();

    createVNCServer();

    String8 v4("127.0.0.1");
    String8 v6("::1");

    setListenAddress(v4, false);
    setListenAddress(v6, true);
}

status_t VNCFlinger::setListenAddress(String8& address, bool v6) {
    if (v6) {
        mVNCScreen->listen6Interface = const_cast<char *>(address.string());
        return NO_ERROR;
    }
    if (!rfbStringToAddr(const_cast<char *>(address.string()), &(mVNCScreen->listenInterface))) {
        return BAD_VALUE;
    }
    return NO_ERROR;
}

status_t VNCFlinger::setPort(unsigned int port) {
    if (port > 65535) {
        port = 0;
    }
    if (port == 0) {
        mVNCScreen->autoPort = 1;
    } else {
        mVNCScreen->autoPort = 0;
        mVNCScreen->port = port;
        mVNCScreen->ipv6port = port;
    }
    return NO_ERROR;
}

status_t VNCFlinger::clearPassword() {
    std::remove(VNC_AUTH_FILE);
    ALOGW("Password authentication disabled");
    return OK;
}

status_t VNCFlinger::setPassword(String8& passwd) {
    String8 path(VNC_AUTH_FILE);
    if (rfbEncryptAndStorePasswd(const_cast<char *>(passwd.string()),
                                 const_cast<char *>(path.string())) != 0) {
        ALOGE("Failed to set password");
        return BAD_VALUE;
    }
    ALOGI("Password has been set");
    return OK;
}

status_t VNCFlinger::start() {
    sp<ProcessState> self = ProcessState::self();
    self->startThreadPool();

    status_t err = startVNCServer();
    if (err != NO_ERROR) {
        ALOGE("Failed to start VNCFlinger: err=%d", err);
        return err;
    }

    rfbRunEventLoop(mVNCScreen, -1, true);

    ALOGD("VNCFlinger ready to fling");

    eventLoop();

    ALOGI("VNCFlinger has left the building");

    return NO_ERROR;
}

status_t VNCFlinger::stop() {
    Mutex::Autolock _L(mEventMutex);

    ALOGV("Shutting down");

    rfbShutdownServer(mVNCScreen, true);

    destroyVirtualDisplayLocked();
    mClientCount = 0;
    mRunning = false;

    mEventCond.signal();
    delete[] mVNCScreen->frameBuffer;
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
        memset(mVNCScreen->frameBuffer, 0, mFrameSize);
        destroyVirtualDisplayLocked();
    }
}

status_t VNCFlinger::createVirtualDisplay() {
    sp<IGraphicBufferConsumer> consumer;
    BufferQueue::createBufferQueue(&mProducer, &consumer);
    mCpuConsumer = new CpuConsumer(consumer, NUM_BUFS);
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
    if (!mVDSActive) {
        return NO_INIT;
    }

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
    mVNCScreen = rfbGetScreen(0, NULL, mWidth, mHeight, 8, 3, 4);
    if (mVNCScreen == NULL) {
        ALOGE("Unable to create VNCScreen");
        return NO_INIT;
    }

    mFrameNumber = 0;
    mFrameSize = mWidth * mHeight * 4;
    mVNCScreen->frameBuffer = (char*)new uint8_t[mFrameSize];
    memset(mVNCScreen->frameBuffer, 0, mFrameSize);

    mVNCScreen->desktopName = "VNCFlinger";
    mVNCScreen->alwaysShared = TRUE;
    mVNCScreen->httpDir = NULL;
    mVNCScreen->authPasswdData = (void *)VNC_AUTH_FILE;
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

    return err;
}

status_t VNCFlinger::startVNCServer() {
    rfbInitServer(mVNCScreen);

    /* Mark as dirty since we haven't sent any updates at all yet. */
    rfbMarkRectAsModified(mVNCScreen, 0, 0, mWidth, mHeight);

    return NO_ERROR;
}

size_t VNCFlinger::addClient() {
    Mutex::Autolock _l(mEventMutex);
    if (mClientCount == 0) {
        mClientCount++;
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

void VNCFlinger::processFrame() {
    // Take the update mutex. This ensures that we don't dequeue
    // a new buffer and blow away the one being sent to a client.
    // The BufferQueue is self-regulating and will drop frames
    // automatically for us.
    Mutex::Autolock _l(mUpdateMutex);

    // get a frame from the virtual display
    CpuConsumer::LockedBuffer imgBuffer;
    status_t res = mCpuConsumer->lockNextBuffer(&imgBuffer);
    if (res != OK) {
        ALOGE("Failed to lock next buffer: %s (%d)", strerror(-res), res);
        return;
    }

    mFrameNumber = imgBuffer.frameNumber;
    ALOGV("processFrame: [%lu] format: %x (%dx%d, stride=%d)", mFrameNumber, imgBuffer.format,
          imgBuffer.width, imgBuffer.height, imgBuffer.stride);

    // we don't know if there was a stride change until we get
    // a buffer from the queue. if it changed, we need to resize
    updateFBSize(imgBuffer);

    // performance is extremely bad if the gpu memory is used
    // directly without copying because it is likely uncached
    memcpy(mVNCScreen->frameBuffer, imgBuffer.data, mFrameSize);

    // update clients
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

    // Set the region of the layer stack we're interested in, which in our
    // case is "all of it".  If the app is rotated (so that the width of the
    // app is based on the height of the display), reverse width/height.
    bool deviceRotated = isDeviceRotated(info.orientation);
    uint32_t sourceWidth, sourceHeight;
    if (!deviceRotated) {
        sourceWidth = info.w;
        sourceHeight = info.h;
    } else {
        ALOGV("using rotated width/height");
        sourceHeight = info.w;
        sourceWidth = info.h;
    }

    if (mWidth == sourceWidth && mHeight == sourceHeight && mOrientation == info.orientation) {
        return false;
    }

    ALOGD("Display dimensions: %dx%d orientation=%d", sourceWidth, sourceHeight, info.orientation);

    // orientation change
    mWidth = sourceWidth;
    mHeight = sourceHeight;
    mOrientation = info.orientation;

    if (!mVDSActive) {
        return true;
    }

    // it does not appear to be possible to reconfigure the virtual display
    // on the fly without forcing surfaceflinger to tear it down
    destroyVirtualDisplayLocked();
    createVirtualDisplay();
    return false;
}

bool VNCFlinger::updateFBSize(CpuConsumer::LockedBuffer& buf) {
    uint32_t stride = (uint32_t)mVNCScreen->paddedWidthInBytes / 4;
    uint32_t width = (uint32_t)mVNCScreen->width;
    uint32_t height = (uint32_t)mVNCScreen->height;

    uint64_t newSize = buf.stride * buf.height * 4;

    if (stride != buf.stride || height != buf.height || width != buf.width) {
        ALOGD("updateFBSize: old=[%dx%d %d] new=[%dx%d %d]", width, height, stride, buf.width,
              buf.height, buf.stride);

        if (mFrameSize != newSize) {
            mFrameSize = newSize;
            delete[] mVNCScreen->frameBuffer;
            rfbNewFramebuffer(mVNCScreen, (char*)new uint8_t[newSize], buf.width, buf.height, 8, 3,
                              4);
        }
        mVNCScreen->paddedWidthInBytes = buf.stride * 4;
    }
    return NO_ERROR;
}

// ------------------------------------------------------------------------ //

// libvncserver logger
void VNCFlinger::rfbLogger(const char* format, ...) {
    va_list args;
    char buf[256];

    va_start(args, format);
    vsprintf(buf, format, args);
    ALOGI("%s", buf);
    va_end(args);
}

// libvncserver callbacks
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

    vf->mFrameStartWhen = systemTime(CLOCK_MONOTONIC);
    ALOGV("frame start [%lu]", vf->mFrameNumber);
}

void VNCFlinger::onFrameDone(rfbClientPtr cl, int /* status */) {
    VNCFlinger* vf = (VNCFlinger*)cl->screen->screenData;

    float timing = (systemTime(CLOCK_MONOTONIC) - vf->mFrameStartWhen) / 1000000.0;
    ALOGV("onFrameDone [%lu] (%.3f ms)", vf->mFrameNumber, timing);

    vf->mUpdateMutex.unlock();
}

// cpuconsumer frame listener
void VNCFlinger::FrameListener::onFrameAvailable(const BufferItem& item) {
    Mutex::Autolock _l(mVNC->mEventMutex);
    mVNC->mFrameAvailable = true;
    mVNC->mEventCond.signal();
    ALOGV("onFrameAvailable: [%lu] mTimestamp=%ld", item.mFrameNumber, item.mTimestamp);
}
