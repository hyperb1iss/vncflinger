LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    src/EglWindow.cpp \
	src/EventQueue.cpp \
    src/Program.cpp \
    src/VirtualDisplay.cpp \
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
    libz \
    libEGL \
    libGLESv3

LOCAL_STATIC_LIBRARIES += \
    libvncserver

LOCAL_CFLAGS := -Ofast -Werror 
LOCAL_CFLAGS += -DLOG_NDEBUG=0

#LOCAL_CXX := /usr/bin/include-what-you-use

LOCAL_MODULE := vncflinger
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
