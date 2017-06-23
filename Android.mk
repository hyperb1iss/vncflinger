LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    src/InputDevice.cpp \
    src/AndroidDesktop.cpp \
    src/main.cpp

#LOCAL_SRC_FILES += \
#    aidl/org/chemlab/IVNCService.aidl

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/src \
    external/tigervnc/common \

LOCAL_SHARED_LIBRARIES := \
    libbinder \
    libcrypto \
    libcutils \
    libgui \
    libjpeg \
    libssl \
    libui \
    libutils \
    libz

LOCAL_STATIC_LIBRARIES += \
    libtigervnc

LOCAL_CFLAGS := -DVNCFLINGER_VERSION="0.1"
LOCAL_CFLAGS += -Ofast -Werror -std=c++11 -fexceptions

LOCAL_CFLAGS += -DLOG_NDEBUG=0
#LOCAL_CXX := /usr/bin/include-what-you-use

LOCAL_INIT_RC := etc/vncflinger.rc

LOCAL_MODULE := vncflinger

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
