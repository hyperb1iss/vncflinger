#ifndef VNCFLINGER_H
#define VNCFLINGER_H

#include "EventQueue.h"
#include "VirtualDisplay.h"

#include <ui/DisplayInfo.h>

#include "rfb/rfb.h"

#define VNC_PORT 5901

namespace android {

class VNCFlinger : public EventListener {
public:
    VNCFlinger(int argc, char **argv) :
            mArgc(argc),
            mArgv(argv),
            mClientCount(0) {
    }

    virtual void onEvent(const Event& event);

    virtual status_t start();
    virtual status_t stop();

    static EventQueue *sQueue;

private:
    virtual status_t setup_l();
    virtual void release_l();

    static ClientGoneHookPtr onClientGone(rfbClientPtr cl);
    static enum rfbNewClientAction onNewClient(rfbClientPtr cl);
    static void rfbLogger(const char *format, ...);
        
    rfbScreenInfoPtr mVNCScreen;
    uint8_t *mVNCBuf;

    uint32_t mWidth, mHeight;
    bool mRotate;

    sp<IBinder> mMainDpy;
    DisplayInfo mMainDpyInfo;
    
    Mutex mMutex;

    sp<VirtualDisplay> mVirtualDisplay;

    int mArgc;
    char **mArgv;

    size_t mClientCount;
};

};
#endif
