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
#include <getopt.h>

#include <csignal>
#include <iostream>

#include "VNCFlinger.h"

using namespace android;

static sp<VNCFlinger> gVNC;

static const char* const shortOpts = "4:6:p:s:vh";
static const option longOpts[] = {
    {"listen", 1, nullptr, '4'},
    {"listen6", 1, nullptr, '6'},
    {"port", 1, nullptr, 'p'},
    {"password", 1, nullptr, 's'},
    {"version", 0, nullptr, 'v'},
    {"help", 0, nullptr, 'h'},
};

static void onSignal(int /* signal */) {
    gVNC->stop();
}

static void printVersion() {
    std::cout << "VNCFlinger-" << VNCFLINGER_VERSION << std::endl;
    exit(0);
}

static void printHelp() {
    std::cout << "Usage: vncflinger [OPTIONS]\n\n"
              << "  -4 <addr>        IPv4 address to listen (default: localhost)\n"
              << "  -6 <addr>        IPv6 address to listen (default: localhost)\n"
              << "  -p <num>         Port to listen on (default: 5900)\n"
              << "  -s <pass>        Store server password\n"
              << "  -c               Clear server password\n"
              << "  -v               Show server version\n"
              << "  -h               Show help\n\n";
    exit(1);
}

static void parseArgs(int argc, char** argv) {
    String8 arg;

    while (true) {
        const auto opt = getopt_long(argc, argv, shortOpts, longOpts, nullptr);

        if (opt < 0) {
            break;
        }

        switch (opt) {
            case 's':
                arg = optarg;
                if (gVNC->setPassword(arg) != OK) {
                    std::cerr << "Failed to set password\n";
                    exit(1);
                }
                exit(0);

            case 'c':
                if (gVNC->clearPassword() != OK) {
                    std::cerr << "Failed to clear password\n";
                    exit(1);
                }
                exit(0);

            case '4':
                arg = optarg;
                if (gVNC->setListenAddress(arg, false) != OK) {
                    std::cerr << "Failed to set IPv4 address\n";
                    exit(1);
                }
                break;

            case '6':
                arg = optarg;
                if (gVNC->setListenAddress(arg, true) != OK) {
                    std::cerr << "Failed to set IPv6 address\n";
                    exit(1);
                }
                break;

            case 'p':
                std::cerr << "port=" << optarg << std::endl;
                if (gVNC->setPort(std::stoi(optarg)) != OK) {
                    std::cerr << "Failed to set port\n";
                    exit(1);
                }
                break;

            case 'v':
                printVersion();
                break;

            case 'h':
            case '?':
            default:
                printHelp();
                break;
        }
    }
}

int main(int argc, char** argv) {
    std::signal(SIGINT, onSignal);
    std::signal(SIGHUP, onSignal);

    gVNC = new VNCFlinger();

    parseArgs(argc, argv);

    gVNC->start();
}
