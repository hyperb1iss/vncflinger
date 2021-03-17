//
// vncflinger - Copyright (C) 2021 Stefanie Kondik
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
#include <utils/RefBase.h>

#include <linux/uinput.h>


#define UINPUT_DEVICE "/dev/uinput"

namespace android {

class InputDevice : public RefBase {
  public:
    virtual status_t start(uint32_t width, uint32_t height);
    virtual status_t start_async(uint32_t width, uint32_t height);
    virtual status_t stop();
    virtual status_t reconfigure(uint32_t width, uint32_t height);

    virtual void keyEvent(bool down, uint32_t key);
    virtual void pointerEvent(int buttonMask, int x, int y);

    InputDevice() : mFD(-1) {
    }
    virtual ~InputDevice() {
        stop();
    }

  private:

    status_t inject(uint16_t type, uint16_t code, int32_t value);
    status_t injectSyn(uint16_t type, uint16_t code, int32_t value);
    status_t movePointer(int32_t x, int32_t y);
    status_t setPointer(int32_t x, int32_t y);
    status_t press(uint16_t code);
    status_t release(uint16_t code);
    status_t click(uint16_t code);

    int keysym2scancode(uint32_t c, int* sh, int* alt);

    Mutex mLock;

    int mFD;
    bool mOpened;

    struct uinput_user_dev mUserDev;

    bool mLeftClicked;
    bool mRightClicked;
    bool mMiddleClicked;
};
};
#endif
