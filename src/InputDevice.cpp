#define LOG_TAG "VNC-InputDevice"
#include <utils/Log.h>

#include "InputDevice.h"

#include <fcntl.h>
#include <stdio.h>

#include <sys/ioctl.h>

#include <linux/input.h>
#include <linux/uinput.h>

#include <rfb/keysym.h>

using namespace android;

static const struct UInputOptions {
    int cmd;
    int bit;
} kOptions[] = {
    {UI_SET_EVBIT, EV_KEY},
    {UI_SET_EVBIT, EV_REP},
    {UI_SET_EVBIT, EV_ABS},
    {UI_SET_EVBIT, EV_SYN},
    {UI_SET_ABSBIT, ABS_X},
    {UI_SET_ABSBIT, ABS_Y},
    {UI_SET_PROPBIT, INPUT_PROP_DIRECT},
};

int InputDevice::sFD = -1;

status_t InputDevice::start(uint32_t width, uint32_t height) {
    status_t err = OK;
    struct uinput_user_dev user_dev;

    struct input_id id = {
        BUS_VIRTUAL, /* Bus type */
        1,           /* Vendor */
        1,           /* Product */
        1,           /* Version */
    };

    if (sFD >= 0) {
        ALOGE("Input device already open!");
        return NO_INIT;
    }

    sFD = open(UINPUT_DEVICE, O_WRONLY | O_NONBLOCK);
    if (sFD < 0) {
        ALOGE("Failed to open %s: err=%d", UINPUT_DEVICE, sFD);
        return NO_INIT;
    }

    unsigned int idx = 0;
    for (idx = 0; idx < sizeof(kOptions) / sizeof(kOptions[0]); idx++) {
        if (ioctl(sFD, kOptions[idx].cmd, kOptions[idx].bit) < 0) {
            ALOGE("uinput ioctl failed: %d %d", kOptions[idx].cmd, kOptions[idx].bit);
            goto err_ioctl;
        }
    }

    for (idx = 0; idx < KEY_MAX; idx++) {
        if (ioctl(sFD, UI_SET_KEYBIT, idx) < 0) {
            ALOGE("UI_SET_KEYBIT failed");
            goto err_ioctl;
        }
    }

    memset(&user_dev, 0, sizeof(user_dev));
    strncpy(user_dev.name, "VNC", UINPUT_MAX_NAME_SIZE);

    user_dev.id = id;

    user_dev.absmin[ABS_X] = 0;
    user_dev.absmax[ABS_X] = width;
    user_dev.absmin[ABS_Y] = 0;
    user_dev.absmax[ABS_Y] = height;

    if (write(sFD, &user_dev, sizeof(user_dev)) != sizeof(user_dev)) {
        ALOGE("Failed to configure uinput device");
        goto err_ioctl;
    }

    if (ioctl(sFD, UI_DEV_CREATE) == -1) {
        ALOGE("UI_DEV_CREATE failed");
        goto err_ioctl;
    }

    return OK;

err_ioctl:
    int prev_errno = errno;
    ::close(sFD);
    errno = prev_errno;
    sFD = -1;
    return NO_INIT;
}

status_t InputDevice::stop() {
    if (sFD < 0) {
        return OK;
    }

    sleep(2);

    ioctl(sFD, UI_DEV_DESTROY);
    close(sFD);
    sFD = -1;

    return OK;
}

status_t InputDevice::inject(uint16_t type, uint16_t code, int32_t value) {
    struct input_event event;
    memset(&event, 0, sizeof(event));
    gettimeofday(&event.time, 0); /* This should not be able to fail ever.. */
    event.type = type;
    event.code = code;
    event.value = value;
    if (write(sFD, &event, sizeof(event)) != sizeof(event)) return BAD_VALUE;
    return OK;
}

status_t InputDevice::injectSyn(uint16_t type, uint16_t code, int32_t value) {
    if (inject(type, code, value) != OK) {
        return BAD_VALUE;
    }
    return inject(EV_SYN, SYN_REPORT, 0);
}

status_t InputDevice::movePointer(int32_t x, int32_t y) {
    if (inject(EV_REL, REL_X, x) != OK) {
        return BAD_VALUE;
    }
    return injectSyn(EV_REL, REL_Y, y);
}

status_t InputDevice::setPointer(int32_t x, int32_t y) {
    if (inject(EV_ABS, ABS_X, x) != OK) {
        return BAD_VALUE;
    }
    return injectSyn(EV_ABS, ABS_Y, y);
}

status_t InputDevice::press(uint16_t code) {
    return inject(EV_KEY, code, 1);
}

status_t InputDevice::release(uint16_t code) {
    return inject(EV_KEY, code, 0);
}

status_t InputDevice::click(uint16_t code) {
    if (press(code) != OK) {
        return BAD_VALUE;
    }
    return release(code);
}

void InputDevice::keyEvent(rfbBool down, rfbKeySym key, rfbClientPtr cl) {
    int code;
    int sh = 0;
    int alt = 0;

    if (sFD < 0) return;

    if ((code = keysym2scancode(key, cl, &sh, &alt))) {
        int ret = 0;

        if (key && down) {
            if (sh) press(42);   // left shift
            if (alt) press(56);  // left alt

            inject(EV_SYN, SYN_REPORT, 0);

            ret = press(code);
            if (ret != 0) {
                ALOGE("Error: %d (%s)\n", errno, strerror(errno));
            }

            inject(EV_SYN, SYN_REPORT, 0);

            ret = release(code);
            if (ret != 0) {
                ALOGE("Error: %d (%s)\n", errno, strerror(errno));
            }

            inject(EV_SYN, SYN_REPORT, 0);

            if (alt) release(56);  // left alt
            if (sh) release(42);   // left shift

            inject(EV_SYN, SYN_REPORT, 0);
        }
    }
}

void InputDevice::pointerEvent(int buttonMask, int x, int y, rfbClientPtr cl) {
    static int leftClicked = 0, rightClicked = 0, middleClicked = 0;
    (void)cl;

    if (sFD < 0) return;

    if ((buttonMask & 1) && leftClicked) {  // left btn clicked and moving
        static int i = 0;
        i = i + 1;

        if (i % 10 == 1)  // some tweak to not report every move event
        {
            inject(EV_ABS, ABS_X, x);
            inject(EV_ABS, ABS_Y, y);
            inject(EV_SYN, SYN_REPORT, 0);
        }
    } else if (buttonMask & 1)  // left btn clicked
    {
        leftClicked = 1;

        inject(EV_ABS, ABS_X, x);
        inject(EV_ABS, ABS_Y, y);
        inject(EV_KEY, BTN_TOUCH, 1);
        inject(EV_SYN, SYN_REPORT, 0);
    } else if (leftClicked)  // left btn released
    {
        leftClicked = 0;
        inject(EV_ABS, ABS_X, x);
        inject(EV_ABS, ABS_Y, y);
        inject(EV_KEY, BTN_TOUCH, 0);
        inject(EV_SYN, SYN_REPORT, 0);
    }

    if (buttonMask & 4)  // right btn clicked
    {
        rightClicked = 1;
        press(158);  // back key
        inject(EV_SYN, SYN_REPORT, 0);
    } else if (rightClicked)  // right button released
    {
        rightClicked = 0;
        release(158);
        inject(EV_SYN, SYN_REPORT, 0);
    }

    if (buttonMask & 2)  // mid btn clicked
    {
        middleClicked = 1;
        press(KEY_END);
        inject(EV_SYN, SYN_REPORT, 0);
    } else if (middleClicked)  // mid btn released
    {
        middleClicked = 0;
        release(KEY_END);
        inject(EV_SYN, SYN_REPORT, 0);
    }
}

// q,w,e,r,t,y,u,i,o,p,a,s,d,f,g,h,j,k,l,z,x,c,v,b,n,m
static const int qwerty[] = {30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
                             49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44};
// ,!,",#,$,%,&,',(,),*,+,,,-,.,/
static const int spec1[] = {57, 2, 40, 4, 5, 6, 8, 40, 10, 11, 9, 13, 51, 12, 52, 52};
static const int spec1sh[] = {0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 1};
// :,;,<,=,>,?,@
static const int spec2[] = {39, 39, 227, 13, 228, 53, 3};
static const int spec2sh[] = {1, 0, 1, 1, 1, 1, 1};
// [,\,],^,_,`
static const int spec3[] = {26, 43, 27, 7, 12, 399};
static const int spec3sh[] = {0, 0, 0, 1, 1, 0};
// {,|,},~
static const int spec4[] = {26, 43, 27, 215, 14};
static const int spec4sh[] = {1, 1, 1, 1, 0};

int InputDevice::keysym2scancode(rfbKeySym c, rfbClientPtr cl, int* sh, int* alt) {
    int real = 1;
    if ('a' <= c && c <= 'z') return qwerty[c - 'a'];
    if ('A' <= c && c <= 'Z') {
        (*sh) = 1;
        return qwerty[c - 'A'];
    }
    if ('1' <= c && c <= '9') return c - '1' + 2;
    if (c == '0') return 11;
    if (32 <= c && c <= 47) {
        (*sh) = spec1sh[c - 32];
        return spec1[c - 32];
    }
    if (58 <= c && c <= 64) {
        (*sh) = spec2sh[c - 58];
        return spec2[c - 58];
    }
    if (91 <= c && c <= 96) {
        (*sh) = spec3sh[c - 91];
        return spec3[c - 91];
    }
    if (123 <= c && c <= 127) {
        (*sh) = spec4sh[c - 123];
        return spec4[c - 123];
    }
    switch (c) {
        case 0xff08:
            return 14;  // backspace
        case 0xff09:
            return 15;  // tab
        case 1:
            (*alt) = 1;
            return 34;  // ctrl+a
        case 3:
            (*alt) = 1;
            return 46;  // ctrl+c
        case 4:
            (*alt) = 1;
            return 32;  // ctrl+d
        case 18:
            (*alt) = 1;
            return 31;  // ctrl+r
        case 0xff0D:
            return 28;  // enter
        case 0xff1B:
            return 158;  // esc -> back
        case 0xFF51:
            return 105;  // left -> DPAD_LEFT
        case 0xFF53:
            return 106;  // right -> DPAD_RIGHT
        case 0xFF54:
            return 108;  // down -> DPAD_DOWN
        case 0xFF52:
            return 103;  // up -> DPAD_UP
        // case 360:
        //	return 232;// end -> DPAD_CENTER (ball click)
        case 0xff50:
            return KEY_HOME;  // home
        case 0xFFC8:
            rfbShutdownServer(cl->screen, TRUE);
            return 0;  // F11 disconnect
        case 0xffff:
            return 158;  // del -> back
        case 0xff55:
            return 229;  // PgUp -> menu
        case 0xffcf:
            return 127;  // F2 -> search
        case 0xffe3:
            return 127;  // left ctrl -> search
        case 0xff56:
            return 61;  // PgUp -> call
        case 0xff57:
            return 107;  // End -> endcall
        case 0xffc2:
            return 211;  // F5 -> focus
        case 0xffc3:
            return 212;  // F6 -> camera
        case 0xffc4:
            return 150;  // F7 -> explorer
        case 0xffc5:
            return 155;  // F8 -> envelope

        case 50081:
        case 225:
            (*alt) = 1;
            if (real) return 48;  // a with acute
            return 30;            // a with acute -> a with ring above

        case 50049:
        case 193:
            (*sh) = 1;
            (*alt) = 1;
            if (real) return 48;  // A with acute
            return 30;            // A with acute -> a with ring above

        case 50089:
        case 233:
            (*alt) = 1;
            return 18;  // e with acute

        case 50057:
        case 201:
            (*sh) = 1;
            (*alt) = 1;
            return 18;  // E with acute

        case 50093:
        case 0xffbf:
            (*alt) = 1;
            if (real) return 36;  // i with acute
            return 23;            // i with acute -> i with grave

        case 50061:
        case 205:
            (*sh) = 1;
            (*alt) = 1;
            if (real) return 36;  // I with acute
            return 23;            // I with acute -> i with grave

        case 50099:
        case 243:
            (*alt) = 1;
            if (real) return 16;  // o with acute
            return 24;            // o with acute -> o with grave

        case 50067:
        case 211:
            (*sh) = 1;
            (*alt) = 1;
            if (real) return 16;  // O with acute
            return 24;            // O with acute -> o with grave

        case 50102:
        case 246:
            (*alt) = 1;
            return 25;  // o with diaeresis

        case 50070:
        case 214:
            (*sh) = 1;
            (*alt) = 1;
            return 25;  // O with diaeresis

        case 50577:
        case 245:
            (*alt) = 1;
            if (real) return 19;  // Hungarian o
            return 25;            // Hungarian o -> o with diaeresis

        case 50576:
        case 213:
            (*sh) = 1;
            (*alt) = 1;
            if (real) return 19;  // Hungarian O
            return 25;            // Hungarian O -> O with diaeresis

        case 50106:
        // case 0xffbe:
        //	(*alt)=1;
        // 	if (real)
        //		return 17; //u with acute
        // 	return 22; //u with acute -> u with grave
        case 50074:
        case 218:
            (*sh) = 1;
            (*alt) = 1;
            if (real) return 17;  // U with acute
            return 22;            // U with acute -> u with grave
        case 50108:
        case 252:
            (*alt) = 1;
            return 47;  // u with diaeresis

        case 50076:
        case 220:
            (*sh) = 1;
            (*alt) = 1;
            return 47;  // U with diaeresis

        case 50609:
        case 251:
            (*alt) = 1;
            if (real) return 45;  // Hungarian u
            return 47;            // Hungarian u -> u with diaeresis

        case 50608:
        case 219:
            (*sh) = 1;
            (*alt) = 1;
            if (real) return 45;  // Hungarian U
            return 47;            // Hungarian U -> U with diaeresis
    }
    return 0;
}
