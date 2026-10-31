#pragma once

// cutenes-server (contracts/transports.md "Headless"): option parsing and
// the run loop, separate from main() so the security tests can run it in
// process.

#include <atomic>
#include <iosfwd>
#include <string>
#include <vector>

namespace cutenes::server {

// Exit codes (transports.md).
inline constexpr int kExitOk = 0;
inline constexpr int kExitUsage = 2;
inline constexpr int kExitBind = 3;
inline constexpr int kExitNoEndpoint = 4;

// Runs until `stop` becomes true. Diagnostics go to `err`; the token never
// does. Returns the process exit code.
int run(const std::vector<std::string>& args, std::ostream& err, const std::atomic<bool>& stop);

} // namespace cutenes::server
