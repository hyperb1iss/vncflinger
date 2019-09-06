#define LOG_TAG "VNCFlinger"
#include <utils/Log.h>

#include <fcntl.h>

#include "AndroidDesktop.h"

#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>

#include <network/Socket.h>
#include <network/TcpSocket.h>
#include <rfb/Configuration.h>
#include <rfb/LogWriter.h>
#include <rfb/Logger_android.h>
#include <rfb/VNCServerST.h>
#include <rfb/util.h>

using namespace vncflinger;
using namespace android;

static char* gProgramName;
static bool gCaughtSignal = false;

static rfb::IntParameter rfbport("rfbport", "TCP port to listen for RFB protocol", 5900);

static void printVersion(FILE* fp) {
    fprintf(fp, "VNCFlinger 1.0");
}

static void usage() {
    printVersion(stderr);
    fprintf(stderr, "\nUsage: %s [<parameters>]\n", gProgramName);
    fprintf(stderr, "       %s --version\n", gProgramName);
    fprintf(stderr,
            "\n"
            "Parameters can be turned on with -<param> or off with -<param>=0\n"
            "Parameters which take a value can be specified as "
            "-<param> <value>\n"
            "Other valid forms are <param>=<value> -<param>=<value> "
            "--<param>=<value>\n"
            "Parameter names are case-insensitive.  The parameters are:\n\n");
    rfb::Configuration::listParams(79, 14);
    exit(1);
}

int main(int argc, char** argv) {
    rfb::initAndroidLogger();
    rfb::LogWriter::setLogParams("*:android:30");

    gProgramName = argv[0];

    rfb::Configuration::enableServerParams();

    for (int i = 1; i < argc; i++) {
        if (rfb::Configuration::setParam(argv[i])) continue;

        if (argv[i][0] == '-') {
            if (i + 1 < argc) {
                if (rfb::Configuration::setParam(&argv[i][1], argv[i + 1])) {
                    i++;
                    continue;
                }
            }
            if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "-version") == 0 ||
                strcmp(argv[i], "--version") == 0) {
                printVersion(stdout);
                return 0;
            }
            usage();
        }
        usage();
    }

    sp<ProcessState> self = ProcessState::self();
    self->startThreadPool();

    std::list<network::SocketListener*> listeners;

    try {
        sp<AndroidDesktop> desktop = new AndroidDesktop();
        rfb::VNCServerST server("vncflinger", desktop.get());
        network::createTcpListeners(&listeners, 0, (int)rfbport);

        int eventFd = desktop->getEventFd();
        fcntl(eventFd, F_SETFL, O_NONBLOCK);

        ALOGI("Listening on port %d", (int)rfbport);

        while (!gCaughtSignal) {
            int wait_ms;
            struct timeval tv;
            fd_set rfds, wfds;
            std::list<network::Socket*> sockets;
            std::list<network::Socket*>::iterator i;

            FD_ZERO(&rfds);
            FD_ZERO(&wfds);

            FD_SET(eventFd, &rfds);
            for (std::list<network::SocketListener*>::iterator i = listeners.begin();
                 i != listeners.end(); i++)
                FD_SET((*i)->getFd(), &rfds);

            server.getSockets(&sockets);
            int clients_connected = 0;
            for (i = sockets.begin(); i != sockets.end(); i++) {
                if ((*i)->isShutdown()) {
                    server.removeSocket(*i);
                    delete (*i);
                } else {
                    FD_SET((*i)->getFd(), &rfds);
                    if ((*i)->outStream().bufferUsage() > 0) FD_SET((*i)->getFd(), &wfds);
                    clients_connected++;
                }
            }

            wait_ms = 0;

            rfb::soonestTimeout(&wait_ms, rfb::Timer::checkTimeouts());

            tv.tv_sec = wait_ms / 1000;
            tv.tv_usec = (wait_ms % 1000) * 1000;

            int n = select(FD_SETSIZE, &rfds, &wfds, 0, wait_ms ? &tv : NULL);

            if (n < 0) {
                if (errno == EINTR) {
                    ALOGV("Interrupted select() system call");
                    continue;
                } else {
                    throw rdr::SystemException("select", errno);
                }
            }

            // Accept new VNC connections
            for (std::list<network::SocketListener*>::iterator i = listeners.begin();
                 i != listeners.end(); i++) {
                if (FD_ISSET((*i)->getFd(), &rfds)) {
                    network::Socket* sock = (*i)->accept();
                    if (sock) {
                        sock->outStream().setBlocking(false);
                        server.addSocket(sock);
                    } else {
                        ALOGW("Client connection rejected");
                    }
                }
            }

            rfb::Timer::checkTimeouts();

            // Client list could have been changed.
            server.getSockets(&sockets);

            // Nothing more to do if there are no client connections.
            if (sockets.empty()) continue;

            // Process events on existing VNC connections
            for (i = sockets.begin(); i != sockets.end(); i++) {
                if (FD_ISSET((*i)->getFd(), &rfds)) server.processSocketReadEvent(*i);
                if (FD_ISSET((*i)->getFd(), &wfds)) server.processSocketWriteEvent(*i);
            }

            // Process events from the display
            uint64_t eventVal;
            int status = read(eventFd, &eventVal, sizeof(eventVal));
            if (status > 0 && eventVal > 0) {
                ALOGV("status=%d eventval=%lu", status, eventVal);
                desktop->processFrames();
            }
        }

    } catch (rdr::Exception& e) {
        ALOGE("%s", e.str());
        return 1;
    }
}
