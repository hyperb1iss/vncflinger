#ifndef MQ_H
#define MQ_H

#include <queue>
#include <vector>

#include <utils/RefBase.h>
#include <utils/Thread.h>

#define EVENT_CLIENT_CONNECT 0
#define EVENT_CLIENT_GONE 1
#define EVENT_BUFFER_READY 2

namespace android {

class Event {
public:
    Event(unsigned int id, void* data = 0):
        mId(id),
        mData(data) {}

    Event(const Event& ev):
        mId(ev.mId),
        mData(ev.mData) {}

    unsigned int mId;
    void* mData;
};

class EventListener {
public:
    virtual ~EventListener() {}

    virtual void onEvent(const Event& event) = 0;
};

class EventQueue : public RefBase {
public:
    EventQueue():
        mRunning(true) {}

    virtual void await();

    virtual void shutdown();

    virtual void enqueue(const Event& event);

    virtual void flush();

    virtual void addListener(EventListener *listener);

    virtual void removeListener(EventListener *listener);

    virtual ~EventQueue() { }

private:
    Mutex mMutex;
    Condition mCondition;

    std::queue<const Event> mQueue;
    std::vector<EventListener *> mListeners;

    bool mRunning;
};

};
#endif
