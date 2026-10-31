#pragma once

// Per-launch HTTP listener tokens (spec FR-024, research R11).

#include <filesystem>
#include <string>

namespace cutenes::api::token {

// 32 bytes from the OS CSPRNG (getrandom / arc4random_buf /
// BCryptGenRandom) as 64 lowercase hex digits. Empty if the OS refused.
std::string generate();

// Compares in time independent of where the inputs first differ.
bool constantTimeEquals(const std::string& a, const std::string& b);

// <user state dir>/cutenes: $XDG_STATE_HOME (else ~/.local/state) on Linux,
// ~/Library/Application Support on macOS, %LOCALAPPDATA% on Windows.
std::filesystem::path defaultDirectory();

// Replaces `path` with {"url": ..., "token": ...}, readable and writable by
// the current user only (mode 0600; a protected owner-only DACL on
// Windows). Creates missing parent directories. On failure returns false
// and sets `error`.
bool writeFile(const std::filesystem::path& path, const std::string& url,
    const std::string& token, std::string& error);

// True if only the current user can access `path` (the check the security
// tests make).
bool isOwnerOnly(const std::filesystem::path& path);

} // namespace cutenes::api::token
