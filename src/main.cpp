#include "VNCFlinger.h"

using namespace android;

int main(int argc, char **argv) {
    VNCFlinger flinger(argc, argv);
    flinger.start();
}
