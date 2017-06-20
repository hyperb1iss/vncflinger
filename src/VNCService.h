#include "org/chemlab/BnVNCService.h"

#include "VNCFlinger.h"

namespace android {

class VNCService : public org::chemlab::BnVNCService {

public:
    VNCService(sp<VNCFlinger> flinger) : mVNC(flinger) {}

    binder::Status start(bool* ret) {
        *ret = mVNC->start();
        return binder::Status::ok();
    }

    binder::Status stop(bool* ret) {
        *ret = mVNC->stop() == NO_ERROR;
        return binder::Status::ok();
    }

    binder::Status setPort(int32_t port, bool* ret) {
        *ret = mVNC->setPort(port) == NO_ERROR;
        return binder::Status::ok();
    }

    binder::Status setV4Address(const String16& addr, bool* ret) {
        *ret = mVNC->setV4Address(String8(addr)) == NO_ERROR;
        return binder::Status::ok();
    }

    binder::Status setV6Address(const String16& addr, bool* ret) {
        *ret = mVNC->setV6Address(String8(addr)) == NO_ERROR;
        return binder::Status::ok();
    }

    binder::Status setPassword(const String16& addr, bool* ret) {
        *ret = mVNC->setPassword(String8(addr)) == NO_ERROR;
        return binder::Status::ok();
    }

    binder::Status clearPassword(bool* ret) {
        *ret = mVNC->clearPassword() == NO_ERROR;
        return binder::Status::ok();
    }

private:
    sp<VNCFlinger> mVNC;
};
};
