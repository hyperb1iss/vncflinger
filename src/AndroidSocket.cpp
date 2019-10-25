#include <AndroidSocket.h>
#include <network/UnixSocket.h>
#include <cutils/sockets.h>

using namespace vncflinger;

AndroidListener::AndroidListener(const char *path) {

    fd = android_get_control_socket(path);
    if (fd < 0) {
		throw network::SocketException("unable to get Android control socket", EADDRNOTAVAIL);
	}

	listen(fd); 
}

AndroidListener::~AndroidListener()
{
}

network::Socket* AndroidListener::createSocket(int fd) {
  return new network::UnixSocket(fd);
}

int AndroidListener::getMyPort() {
  return 0;
}

