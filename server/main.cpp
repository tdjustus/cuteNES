// cutenes-server: the headless MCP and API server (feature 002).

#include "server.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <iostream>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

std::atomic<bool> g_stop{false};

extern "C" void onSignal(int) { g_stop.store(true); }

} // namespace

int main(int argc, char** argv)
{
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
#ifdef _WIN32
    // MCP stdio lines end in "\n", not the text mode's "\r\n".
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#else
    // A client that exits closes the pipe; the write fails instead of
    // killing the server.
    std::signal(SIGPIPE, SIG_IGN);
#endif
    const std::vector<std::string> args(argv + 1, argv + argc);
    return cutenes::server::run(args, std::cin, std::cout, std::cerr, g_stop);
}
