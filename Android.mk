LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    src/InputDevice.cpp \
    src/VNCFlinger.cpp \
    src/main.cpp

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/src \
    external/libvncserver \
    external/zlib

LOCAL_SHARED_LIBRARIES := \
    libbinder \
    libcrypto \
    libcutils \
    libjpeg \
    libgui \
    libpng \
    libssl \
    libui \
    libutils \
    libz

LOCAL_STATIC_LIBRARIES += \
    libvncserver

LOCAL_CFLAGS := -Ofast -Werror -std=c++11

#LOCAL_CFLAGS += -DLOG_NDEBUG=0
#LOCAL_CXX := /usr/bin/include-what-you-use

LOCAL_INIT_RC := etc/vncflinger.rc

LOCAL_MODULE := vncflinger

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
