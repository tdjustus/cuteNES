// cutenes-server: the headless API (and, from feature 002 P3, MCP) server.

#include "server.hpp"

#include <atomic>
#include <csignal>
#include <iostream>

namespace {

std::atomic<bool> g_stop{false};

extern "C" void onSignal(int) { g_stop.store(true); }

} // namespace

int main(int argc, char** argv)
{
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    const std::vector<std::string> args(argv + 1, argv + argc);
    return cutenes::server::run(args, std::cerr, g_stop);
}
