#include <gui/CpuConsumer.h>

namespace vncflinger {

class VirtualDisplay {
  public:
    VirtualDisplay();

    virtual void onFrameAvailable(const BufferItem& item);

  private:
    Mutex mEventMutex;
    Condition mEventCond;

    bool mFrameAvailable;

    // Virtual display
    sp<IBinder> mDpy;

    // Producer side of queue, passed into the virtual display.
    sp<IGraphicBufferProducer> mProducer;

    // This receives frames from the virtual display and makes them available
    sp<CpuConsumer> mCpuConsumer;
};
};
