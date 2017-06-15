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
            mClientCount(0),
            mOrientation(-1) {
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
    virtual status_t destroyVirtualDisplayLocked();
    virtual status_t createVNCServer();

    virtual void processFrame();

    virtual bool isDeviceRotated(int orientation);
    virtual bool updateDisplayProjection();
    virtual status_t updateFBSize(int width, int height, int stride);

    // vncserver callbacks
    static ClientGoneHookPtr onClientGone(rfbClientPtr cl);
    static enum rfbNewClientAction onNewClient(rfbClientPtr cl);
    static void onFrameStart(rfbClientPtr cl);
    static void onFrameDone(rfbClientPtr cl, int result);
    static void rfbLogger(const char *format, ...);

    bool mRunning;
    bool mFrameAvailable;
    bool mRotate;
    bool mVDSActive;
    bool mInputReconfigPending;

    Mutex mEventMutex;
    Mutex mUpdateMutex;

    Condition mEventCond;

    rfbScreenInfoPtr mVNCScreen;
    uint8_t *mVNCBuf;

    int mWidth, mHeight;

    sp<IBinder> mMainDpy;

    int mArgc;
    char **mArgv;

    size_t mClientCount;

    int mOrientation;

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
