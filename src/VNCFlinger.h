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
#include <utils/String8.h>

#include <rfb/rfb.h>
#undef max

#define VNC_AUTH_FILE "/data/system/vncauth"
#define NUM_BUFS 1

namespace android {

class VNCFlinger : public RefBase {
  public:
    VNCFlinger();

    virtual ~VNCFlinger() {
    }

    virtual status_t start();
    virtual status_t stop();

    virtual size_t addClient();
    virtual size_t removeClient();

    virtual status_t setPort(unsigned int port);
    virtual status_t setV4Address(const String8& address);
    virtual status_t setV6Address(const String8& address);

    virtual status_t clearPassword();
    virtual status_t setPassword(const String8& passwd);

  private:
    class FrameListener : public CpuConsumer::FrameAvailableListener {
      public:
        FrameListener(VNCFlinger* vnc) : mVNC(vnc) {
        }

        virtual void onFrameAvailable(const BufferItem& item);

      private:
        FrameListener(FrameListener&) {
        }
        VNCFlinger* mVNC;
    };

    virtual void eventLoop();

    virtual status_t createVirtualDisplay();
    virtual status_t destroyVirtualDisplayLocked();

    virtual status_t createVNCServer();
    virtual status_t startVNCServer();

    virtual void processFrame();

    virtual bool isDeviceRotated(int orientation);
    virtual bool updateDisplayProjection();
    virtual bool updateFBSize(CpuConsumer::LockedBuffer& buf);

    // vncserver callbacks
    static ClientGoneHookPtr onClientGone(rfbClientPtr cl);
    static enum rfbNewClientAction onNewClient(rfbClientPtr cl);
    static void onFrameStart(rfbClientPtr cl);
    static void onFrameDone(rfbClientPtr cl, int result);
    static void rfbLogger(const char* format, ...);

    bool mRunning;
    bool mFrameAvailable;
    bool mRotate;
    bool mVDSActive;

    Mutex mEventMutex;
    Mutex mUpdateMutex;

    Condition mEventCond;

    uint32_t mWidth, mHeight;
    int32_t mOrientation;

    size_t mClientCount;

    // Framebuffers
    uint64_t mFrameNumber;
    uint64_t mFrameSize;
    nsecs_t mFrameStartWhen;

    // Server instance
    rfbScreenInfoPtr mVNCScreen;

    // Primary display
    sp<IBinder> mMainDpy;

    // Virtual display
    sp<IBinder> mDpy;

    // Producer side of queue, passed into the virtual display.
    sp<IGraphicBufferProducer> mProducer;

    // This receives frames from the virtual display and makes them available
    sp<CpuConsumer> mCpuConsumer;

    // Consumer callback
    sp<FrameListener> mListener;
};
};
#endif
