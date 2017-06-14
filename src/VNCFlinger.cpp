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
    Mutex::Autolock _l(mMutex);

    status_t err = setup_l();
    if (err != NO_ERROR) {
        ALOGE("Failed to start VNCFlinger: err=%d", err);
        return err;
    }

    ALOGD("VNCFlinger is running!");

    rfbRunEventLoop(mVNCScreen, -1, true);

    mCondition.wait(mMutex);

    release_l();
    return NO_ERROR;
}

status_t VNCFlinger::setup_l() {

    status_t err = NO_ERROR;

    mMainDpy = SurfaceComposerClient::getBuiltInDisplay(
            ISurfaceComposer::eDisplayIdMain);
    err = SurfaceComposerClient::getDisplayInfo(mMainDpy, &mMainDpyInfo);
    if (err != NO_ERROR) {
        ALOGE("Unable to get display characteristics\n");
        return err;
    }

    bool rotated = VirtualDisplay::isDeviceRotated(mMainDpyInfo.orientation);
    if (mWidth == 0) {
        mWidth = rotated ? mMainDpyInfo.h : mMainDpyInfo.w;
    }
    if (mHeight == 0) {
        mHeight = rotated ? mMainDpyInfo.w : mMainDpyInfo.h;
    }

    ALOGD("Display dimensions: %dx%d rotated=%d", mWidth, mHeight, rotated);

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
    mVNCScreen->serverFormat.trueColour = true;
    mVNCScreen->serverFormat.bitsPerPixel = 32;
    mVNCScreen->handleEventsEagerly = true;
    mVNCScreen->deferUpdateTime = 16;
    mVNCScreen->screenData = this;

    rfbInitServer(mVNCScreen);

    /* Mark as dirty since we haven't sent any updates at all yet. */
    rfbMarkRectAsModified(mVNCScreen, 0, 0, mWidth, mHeight);


    mVirtualDisplay = new VirtualDisplay(mVNCScreen);

    return err;
}

void VNCFlinger::release_l() {
    mVirtualDisplay.clear();

    ALOGD("VNCFlinger released");
}

status_t VNCFlinger::stop() {
    Mutex::Autolock _l(mMutex);
    mCondition.signal();

    return NO_ERROR;
}

size_t VNCFlinger::addClient() {
    Mutex::Autolock _l(mMutex);
    if (mClientCount == 0) {
        InputDevice::start(mWidth, mHeight);
        mVirtualDisplay->start(mMainDpyInfo);
    }
    mClientCount++;

    ALOGI("Client connected (%zu)", mClientCount);

    return mClientCount;
}

size_t VNCFlinger::removeClient() {
    Mutex::Autolock _l(mMutex);
    if (mClientCount > 0) {
        mClientCount--;
        if (mClientCount == 0) {
            mVirtualDisplay->stop();
            InputDevice::stop();
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

void VNCFlinger::rfbLogger(const char *format, ...) {
    va_list args;
    char buf[256];

    va_start(args, format);
    vsprintf(buf, format, args);
    ALOGI("%s", buf);
    va_end(args);
}
