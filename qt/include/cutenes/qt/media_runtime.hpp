#pragma once

// SDL's audio and gamepad subsystems for the life of the object (research
// R4, R5). The application holds exactly one. Qt owns every window, so SDL
// runs without its video subsystem.

#include <string>

namespace cutenes::qt {

class MediaRuntime {
public:
    MediaRuntime();
    ~MediaRuntime();
    MediaRuntime(const MediaRuntime&) = delete;
    MediaRuntime& operator=(const MediaRuntime&) = delete;

    // False when no audio driver started: the emulator then runs silently
    // and says so (spec US7-5).
    bool audioAvailable() const { return m_audio; }
    bool gamepadsAvailable() const { return m_gamepads; }
    // The SDL audio driver in use ("coreaudio", "wasapi", "pipewire", ...),
    // or empty.
    std::string audioDriver() const;
    // What failed, when a subsystem did not start.
    const std::string& error() const { return m_error; }

private:
    bool m_audio = false;
    bool m_gamepads = false;
    std::string m_error;
};

} // namespace cutenes::qt
