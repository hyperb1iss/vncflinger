#ifndef ANDROID_DESKTOP_H_
#define ANDROID_DESKTOP_H_

#include <memory>

#include <utils/Condition.h>
#include <utils/Mutex.h>
#include <utils/RefBase.h>
#include <utils/Thread.h>

#include <gui/CpuConsumer.h>

#include <rfb/PixelBuffer.h>
#include <rfb/SDesktop.h>
#include <rfb/VNCServerST.h>

#include "InputDevice.h"


using namespace android;

namespace vncflinger {

static const rfb::PixelFormat pfRGBX(32, 24, false, true, 255, 255, 255, 0, 8, 16);

class AndroidDesktop : public rfb::SDesktop, public RefBase {
  public:
    AndroidDesktop();

    virtual ~AndroidDesktop();

    virtual void start(rfb::VNCServer* vs);
    virtual void stop();

    virtual rfb::Point getFbSize();
    virtual unsigned int setScreenLayout(int fb_width, int fb_height, const rfb::ScreenSet& layout);

    virtual void keyEvent(rdr::U32 key, bool down);
    virtual void pointerEvent(const rfb::Point& pos, int buttonMask);

    virtual void processFrames();

    virtual int getEventFd() {
        return mEventFd;
    }

  private:
    class FrameListener : public CpuConsumer::FrameAvailableListener {
      public:
        FrameListener(AndroidDesktop* desktop) : mDesktop(desktop) {
        }

        virtual void onFrameAvailable(const BufferItem& item);

      private:
        FrameListener(FrameListener&) {
        }
        AndroidDesktop* mDesktop;
    };

    class AndroidPixelBuffer : public RefBase, public rfb::ManagedPixelBuffer {
      public:
        AndroidPixelBuffer(uint64_t width, uint64_t height)
            : rfb::ManagedPixelBuffer(sRGBX, width, height) {
        }
    };

    virtual status_t createVirtualDisplay();
    virtual status_t destroyVirtualDisplay();

    virtual void notify();

    virtual status_t updateDisplayProjection();
    virtual bool updateFBSize(uint64_t width, uint64_t height);
    virtual void processDesktopResize();

    uint64_t mSourceWidth, mSourceHeight;
    uint64_t mWidth, mHeight;
    Rect mDisplayRect;

    bool mRotated;

    Mutex mMutex;

    bool mFrameAvailable;
    bool mProjectionChanged;
    bool mRotate;
    bool mVDSActive;

    uint64_t mFrameNumber;
    nsecs_t mFrameStartWhen;

    int mEventFd;

    // Android virtual display is always 32-bit
    static const rfb::PixelFormat sRGBX;

    // Server instance
    rfb::VNCServerST* mServer;

    // Pixel buffer
    sp<AndroidPixelBuffer> mPixels;

    // Primary display
    sp<IBinder> mMainDpy;

    // Virtual display
    sp<IBinder> mDpy;

    // Producer side of queue, passed into the virtual display.
    sp<IGraphicBufferProducer> mProducer;

    // This receives frames from the virtual display and makes them available
    sp<CpuConsumer> mCpuConsumer;

    // Listener for virtual display buffers
    sp<FrameListener> mListener;

    // Virtual input device
    sp<InputDevice> mInputDevice;
};
};

#endif
