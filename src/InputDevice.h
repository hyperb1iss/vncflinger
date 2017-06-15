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

#ifndef INPUT_DEVICE_H
#define INPUT_DEVICE_H

#include <utils/Errors.h>
#include <utils/Mutex.h>
#include <utils/Singleton.h>

#include <rfb/rfb.h>
#include <linux/uinput.h>

#define UINPUT_DEVICE "/dev/uinput"

namespace android {

class InputDevice : public Singleton<InputDevice> {

friend class Singleton;

public:
    virtual status_t start(uint32_t width, uint32_t height);
    virtual status_t stop();
    virtual status_t reconfigure(uint32_t width, uint32_t height);

    static void onKeyEvent(rfbBool down, rfbKeySym key, rfbClientPtr cl);
    static void onPointerEvent(int buttonMask, int x, int y, rfbClientPtr cl);

    InputDevice() : mFD(-1) {}
    virtual ~InputDevice() {}

private:

    status_t inject(uint16_t type, uint16_t code, int32_t value);
    status_t injectSyn(uint16_t type, uint16_t code, int32_t value);
    status_t movePointer(int32_t x, int32_t y);
    status_t setPointer(int32_t x, int32_t y);
    status_t press(uint16_t code);
    status_t release(uint16_t code);
    status_t click(uint16_t code);

    void keyEvent(rfbBool down, rfbKeySym key, rfbClientPtr cl);
    void pointerEvent(int buttonMask, int x, int y, rfbClientPtr cl);

    int keysym2scancode(rfbKeySym c, rfbClientPtr cl, int* sh, int* alt);

    Mutex mLock;

    int mFD;

    struct uinput_user_dev mUserDev;
};

};
#endif
