#ifndef VNCFLINGER_H
#define VNCFLINGER_H

#include <gui/CpuConsumer.h>
#include <ui/DisplayInfo.h>

#include "rfb/rfb.h"

#define VNC_PORT 5901

namespace android {

class VNCFlinger {
public:
    VNCFlinger(int argc, char **argv) :
            mArgc(argc),
            mArgv(argv),
            mClientCount(0) {
    }

    virtual ~VNCFlinger() {}

    virtual status_t start();
    virtual status_t stop();

    virtual size_t addClient();
    virtual size_t removeClient();


private:

    class FrameListener : public CpuConsumer::FrameAvailableListener {
    public:
        FrameListener(VNCFlinger *vnc) : mVNC(vnc) {}

        virtual void onFrameAvailable(const BufferItem& item);

    private:
        FrameListener(FrameListener&) {}
        VNCFlinger *mVNC;
    };

    virtual void eventLoop();

    virtual status_t createVirtualDisplay();
    virtual status_t destroyVirtualDisplay();
    virtual status_t createVNCServer();

    virtual void processFrame();

    // vncserver callbacks
    static ClientGoneHookPtr onClientGone(rfbClientPtr cl);
    static enum rfbNewClientAction onNewClient(rfbClientPtr cl);
    static void onFrameStart(rfbClientPtr cl);
    static void onFrameDone(rfbClientPtr cl, int result);
    static void rfbLogger(const char *format, ...);

    bool mRunning;
    bool mFrameAvailable;

    Mutex mEventMutex;
    Mutex mUpdateMutex;

    Condition mEventCond;

    rfbScreenInfoPtr mVNCScreen;
    uint8_t *mVNCBuf;

    uint32_t mWidth, mHeight;

    sp<IBinder> mMainDpy;
    DisplayInfo mMainDpyInfo;

    int mArgc;
    char **mArgv;

    size_t mClientCount;

    sp<FrameListener> mListener;

    // Producer side of queue, passed into the virtual display.
    sp<IGraphicBufferProducer> mProducer;

    // This receives frames from the virtual display and makes them available
    sp<CpuConsumer> mCpuConsumer;

    // The virtual display instance
    sp<IBinder> mDpy;

};

};
#endif
