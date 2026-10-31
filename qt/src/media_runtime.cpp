#include <cutenes/qt/media_runtime.hpp>

#include <SDL3/SDL.h>
// SDL_SetMainReady lives here. SDL_MAIN_HANDLED keeps the header from
// renaming the application's main().
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

namespace cutenes::qt {

MediaRuntime::MediaRuntime()
{
    // cuteNES has no SDL_main: tell SDL the process entry point is ready.
    SDL_SetMainReady();
    // SDL has no window of its own here, so without this hint it would
    // ignore gamepads whenever "its" window is not focused, which is always.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    // Qt owns the process lifecycle. SDL's SIGINT/SIGTERM handlers would turn
    // those signals into SDL quit events nobody reads, and the process would
    // ignore kill and Ctrl+C.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

    m_audio = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!m_audio) {
        m_error = std::string("audio: ") + SDL_GetError();
    }
    m_gamepads = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    if (!m_gamepads) {
        if (!m_error.empty()) m_error += "; ";
        m_error += std::string("gamepads: ") + SDL_GetError();
    }
}

MediaRuntime::~MediaRuntime()
{
    if (m_gamepads) SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    if (m_audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    SDL_Quit();
}

std::string MediaRuntime::audioDriver() const
{
    const char* driver = m_audio ? SDL_GetCurrentAudioDriver() : nullptr;
    return driver ? driver : "";
}

} // namespace cutenes::qt
