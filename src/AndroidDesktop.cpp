#define LOG_TAG "AndroidDesktop"
#include <utils/Log.h>

#include <fcntl.h>
#include <sys/eventfd.h>

#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>

#include <gui/ISurfaceComposer.h>
#include <gui/SurfaceComposerClient.h>

#include <ui/DisplayInfo.h>

#include <rfb/PixelFormat.h>
#include <rfb/Rect.h>
#include <rfb/ScreenSet.h>
#include <rfb/VNCServerST.h>

#include "AndroidDesktop.h"
#include "InputDevice.h"

using namespace vncflinger;
using namespace android;

const rfb::PixelFormat AndroidDesktop::sRGBX(32, 24, false, true, 255, 255, 255, 0, 8, 16);

AndroidDesktop::AndroidDesktop() {
    mListener = new FrameListener(this);
    mInputDevice = new InputDevice();

    mEventFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (mEventFd < 0) {
        ALOGE("Failed to create event notifier");
        return;
    }
}

AndroidDesktop::~AndroidDesktop() {
    mInputDevice->stop();
    close(mEventFd);
}

void AndroidDesktop::start(rfb::VNCServer* vs) {
    Mutex::Autolock _l(mMutex);

    sp<ProcessState> self = ProcessState::self();
    self->startThreadPool();

    mMainDpy = SurfaceComposerClient::getBuiltInDisplay(ISurfaceComposer::eDisplayIdMain);
    if (updateDisplayProjection() == NO_INIT) {
        ALOGE("Failed to query display!");
        return;
    }
    mProjectionChanged = false;

    status_t err = createVirtualDisplay();
    if (err != NO_ERROR) {
        ALOGE("Failed to create virtual display: err=%d", err);
        return;
    }

    mInputDevice->start_async(mWidth, mHeight);

    mServer = (rfb::VNCServerST*)vs;

    updateFBSize(mWidth, mHeight);

    mServer->setPixelBuffer(mPixels.get());

    ALOGV("Desktop is running");
}

void AndroidDesktop::stop() {
    Mutex::Autolock _L(mMutex);

    ALOGV("Shutting down");

    mServer->setPixelBuffer(0);
    destroyVirtualDisplay();
    mWidth = 0;
    mHeight = 0;
}

status_t AndroidDesktop::createVirtualDisplay() {
    sp<IGraphicBufferConsumer> consumer;
    BufferQueue::createBufferQueue(&mProducer, &consumer);
    mCpuConsumer = new CpuConsumer(consumer, 1);
    mCpuConsumer->setName(String8("vds-to-cpu"));
    mCpuConsumer->setDefaultBufferSize(mWidth, mHeight);
    mProducer->setMaxDequeuedBufferCount(4);
    consumer->setDefaultBufferFormat(PIXEL_FORMAT_RGBX_8888);

    mCpuConsumer->setFrameAvailableListener(mListener);

    mDpy = SurfaceComposerClient::createDisplay(String8("VNC-VirtualDisplay"), false /*secure*/);

    // aspect ratio
    float displayAspect = (float) mSourceHeight / (float) mSourceWidth;

    uint32_t outWidth, outHeight;
    if (mWidth > (uint32_t)(mWidth * displayAspect)) {
        // limited by narrow width; reduce height
        outWidth = mWidth;
        outHeight = (uint32_t)(mWidth * displayAspect);
    } else {
        // limited by short height; restrict width
        outHeight = mHeight;
        outWidth = (uint32_t)(mHeight / displayAspect);
    }

    // position the desktop in the viewport while preserving
    // the source aspect ratio. we do this in case the client
    // has resized the window and to deal with orientation
    // changes set up by updateDisplayProjection
    uint32_t offX, offY;
    offX = (mWidth - outWidth) / 2;
    offY = (mHeight - outHeight) / 2;
    mDisplayRect = Rect(offX, offY, offX + outWidth, offY + outHeight);
    Rect sourceRect(0, 0, mSourceWidth, mSourceHeight);

    SurfaceComposerClient::openGlobalTransaction();
    SurfaceComposerClient::setDisplaySurface(mDpy, mProducer);
    SurfaceComposerClient::setDisplayProjection(mDpy, 0, sourceRect, mDisplayRect);
    SurfaceComposerClient::setDisplayLayerStack(mDpy, 0);  // default stack
    SurfaceComposerClient::closeGlobalTransaction();

    mVDSActive = true;

    ALOGV("Virtual display (%lux%lu [viewport=%ux%u] created", mWidth, mHeight,
            outWidth, outHeight);

    return NO_ERROR;
}

status_t AndroidDesktop::destroyVirtualDisplay() {
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

void AndroidDesktop::processDesktopResize() {
    if (mProjectionChanged) {
        destroyVirtualDisplay();
        createVirtualDisplay();
        updateFBSize(mWidth, mHeight);
        mInputDevice->reconfigure(mDisplayRect.getWidth(), mDisplayRect.getHeight());
        rfb::ScreenSet screens;
        screens.add_screen(rfb::Screen(0, 0, 0, mWidth, mHeight, 0));
        mServer->setScreenLayout(screens);

        mProjectionChanged = false;
    }
}

void AndroidDesktop::processFrames() {
    Mutex::Autolock _l(mMutex);

    // do any pending resize
    processDesktopResize();

    if (!mFrameAvailable) {
        return;
    }

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

    rfb::Rect bufRect(0, 0, imgBuffer.width, imgBuffer.height);

    // performance is extremely bad if the gpu memory is used
    // directly without copying because it is likely uncached
    mPixels->imageRect(bufRect, imgBuffer.data, imgBuffer.stride);

    mCpuConsumer->unlockBuffer(imgBuffer);

    // update clients
    mServer->add_changed(bufRect);
    mFrameAvailable = false;
}

// notifies the server loop that we have changes
void AndroidDesktop::notify() {
    static uint64_t notify = 1;
    write(mEventFd, &notify, sizeof(notify));
}

// called when a client resizes the window
unsigned int AndroidDesktop::setScreenLayout(int reqWidth, int reqHeight,
                                             const rfb::ScreenSet& layout) {
    Mutex::Autolock _l(mMutex);

    char* dbg = new char[1024];
    layout.print(dbg, 1024);

    ALOGD("setScreenLayout: cur: %lux%lu  new: %dx%d %s", mWidth, mHeight, reqWidth, reqHeight, dbg);
    delete[] dbg;

    if (reqWidth == (int)mWidth && reqHeight == (int)mHeight) {
        return rfb::resultInvalid;
    }

    if (reqWidth > 0 && reqHeight > 0) {
        mWidth = reqWidth;
        mHeight = reqHeight;

        if (updateDisplayProjection() == NO_ERROR) {
            // resize immediately
            processDesktopResize();
            notify();
            return rfb::resultSuccess;
        }
    }
    return rfb::resultInvalid;
}

// updates the pixelbuffer dimensions
bool AndroidDesktop::updateFBSize(uint64_t width, uint64_t height) {
    if (mPixels == nullptr || mPixels->height() != (int)height || mPixels->width() != (int)width) {
        if (mPixels != nullptr) {
            ALOGD("updateFBSize: old=[%dx%d] new=[%lux%lu]", mPixels->width(), mPixels->height(),
                  width, height);
        }
        if (mPixels != nullptr && (int)width <= mPixels->width() &&
            (int)height <= mPixels->height()) {
            mPixels->setSize(width, height);
        } else {
            mPixels = new AndroidPixelBuffer(width, height);
            mServer->setPixelBuffer(mPixels.get());
        }
        return true;
    }
    return false;
}

// cpuconsumer frame listener, called from binder thread
void AndroidDesktop::FrameListener::onFrameAvailable(const BufferItem& item) {
    Mutex::Autolock _l(mDesktop->mMutex);
    mDesktop->updateDisplayProjection();
    mDesktop->mFrameAvailable = true;
    mDesktop->notify();
    ALOGV("onFrameAvailable: [%lu] mTimestamp=%ld", item.mFrameNumber, item.mTimestamp);
}

rfb::Point AndroidDesktop::getFbSize() {
    return rfb::Point(mPixels->width(), mPixels->height());
}

void AndroidDesktop::keyEvent(rdr::U32 key, bool down) {
    mInputDevice->keyEvent(down, key);
}

void AndroidDesktop::pointerEvent(const rfb::Point& pos, int buttonMask) {
    if (pos.x < mDisplayRect.left || pos.x > mDisplayRect.right ||
            pos.y < mDisplayRect.top || pos.y > mDisplayRect.bottom) {
        // outside viewport
        return;
    }
    uint32_t x = pos.x * ((float)(mDisplayRect.getWidth()) / (float)mWidth);
    uint32_t y = pos.y * ((float)(mDisplayRect.getHeight()) / (float)mHeight);

    ALOGD("pointer xlate x1=%d y1=%d x2=%d y2=%d", pos.x, pos.y, x, y);

    mServer->setCursorPos(rfb::Point(x, y));
    mInputDevice->pointerEvent(buttonMask, x, y);
}

// figure out the dimensions of the display. deal with orientation
// changes, client-side window resize, server-side scaling, and
// maintaining aspect ratio.
status_t AndroidDesktop::updateDisplayProjection() {
    DisplayInfo info;
    status_t err = SurfaceComposerClient::getDisplayInfo(mMainDpy, &info);
    if (err != NO_ERROR) {
        ALOGE("Failed to get display characteristics\n");
        return err;
    }

    bool deviceRotated =
        info.orientation != DISPLAY_ORIENTATION_0 && info.orientation != DISPLAY_ORIENTATION_180;

    // if orientation changed, swap width/height
    uint32_t sourceWidth, sourceHeight;
    if (!deviceRotated) {
        sourceWidth = info.w;
        sourceHeight = info.h;
    } else {
        sourceHeight = info.w;
        sourceWidth = info.h;
    }

    if (mWidth == 0 && mHeight == 0) {
        mWidth = sourceWidth;
        mHeight = sourceHeight;
    }

    if (deviceRotated != mRotated) {
        std::swap(mWidth, mHeight);
        mRotated = deviceRotated;
    }

    // if nothing changed, we're done
    if (mSourceWidth == sourceWidth && mSourceHeight == sourceHeight &&
        (int)mWidth == mPixels->width() && (int)mHeight == mPixels->height()) {
        return NO_ERROR;
    }

    // update all the values and flag for an update
    mSourceWidth = sourceWidth;
    mSourceHeight = sourceHeight;
    mProjectionChanged = true;

    ALOGV("Dimensions: %lux%lu [out: %lux%lu] rotated=%d", mSourceWidth, mSourceHeight, mWidth,
          mHeight, mRotated);

    return NO_ERROR;
}
