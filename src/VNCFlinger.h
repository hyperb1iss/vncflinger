#ifndef VNCFLINGER_H
#define VNCFLINGER_H

#include "VirtualDisplay.h"

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

    virtual void markFrame(void* frame, size_t stride);

private:

    virtual status_t setup_l();
    virtual void release_l();

    static ClientGoneHookPtr onClientGone(rfbClientPtr cl);
    static enum rfbNewClientAction onNewClient(rfbClientPtr cl);
    static void onFrameStart(rfbClientPtr cl);
    static void onFrameDone(rfbClientPtr cl, int result);
    static void rfbLogger(const char *format, ...);

    Condition mCondition;

    rfbScreenInfoPtr mVNCScreen;
    uint8_t *mVNCBuf;

    uint32_t mWidth, mHeight;
    bool mRotate;

    sp<IBinder> mMainDpy;
    DisplayInfo mMainDpyInfo;
    
    Mutex mMutex;
    static Mutex sUpdateMutex;

    sp<VirtualDisplay> mVirtualDisplay;

    int mArgc;
    char **mArgv;

    size_t mClientCount;
};

};
#endif
