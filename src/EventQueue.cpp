#define LOG_TAG "VNC-EventQueue"
#include <utils/Log.h>

#include "EventQueue.h"

using namespace android;

void EventQueue::enqueue(const Event& event) {
    mQueue.push(event);
    ALOGV("enqueue: mId=%d mData=%p qlen=%zu", event.mId, event.mData, mQueue.size());

    Mutex::Autolock _l(mMutex);
    mCondition.broadcast();
}

void EventQueue::await() {
    Mutex::Autolock _l(mMutex);

    while (mRunning) {
        ALOGV("begin wait");
        mCondition.wait(mMutex);

        ALOGV("queue active");
        while (!mQueue.empty()) {
            Event event = mQueue.front();
            mQueue.pop();

            mMutex.unlock();
            for (std::vector<EventListener *>::iterator it = mListeners.begin();
                    it != mListeners.end(); ++it) {
                ALOGV("call listener: %p", *it);
                (*it)->onEvent(event);
            }
            mMutex.lock();

        }
    }
}

void EventQueue::shutdown() {
    Mutex::Autolock _l(mMutex);
    flush();
    mRunning = false;
    mCondition.broadcast();
}

void EventQueue::flush() {
    Mutex::Autolock _l(mMutex);
    mQueue = {};
}

void EventQueue::addListener(EventListener *listener) {
    mListeners.push_back(listener);
    ALOGV("addListener: %p", listener);
}

void EventQueue::removeListener(EventListener *listener) {
    mListeners.erase(std::remove(mListeners.begin(), mListeners.end(), listener), mListeners.end());
}
