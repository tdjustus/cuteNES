#pragma once

// The GUI's host commands (research R11, contracts/menus-and-commands.md).
// Every menu action triggers one gui.* command, so a click and an AI's call
// run the same code. Handlers run on the session's emulation thread: machine
// work runs inline through core.* commands, and widget work goes through the
// Window interface, which posts it to the GUI thread. Nothing here waits for
// the GUI.

#include "library/library.hpp"
#include "library/library_index.hpp"
#include "media/movie.hpp"
#include "media/wav_writer.hpp"
#include "persistence/battery_saver.hpp"
#include "cheats/cheat_list.hpp"
#include "cheats/cheat_search.hpp"
#include "persistence/rewind_buffer.hpp"
#include "persistence/state_slots.hpp"

#include <cutenes/api/host.hpp>
#include <cutenes/api/registry.hpp>
#include <cutenes/api/session.hpp>

#include <QString>
#include <QVector>

#include <atomic>
#include <bitset>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cutenes::app {

class AppSettings;
class KeyBindings;

class GuiHost final : public api::IHost {
public:
    // What the commands need from the window. Called on the emulation
    // thread: the show* and notification calls post their widget work and
    // return at once; the getters are thread-safe.
    class Window {
    public:
        virtual ~Window() = default;
        virtual void showOpenRomDialog() = 0;
        // The file dialog of a command given no path.
        enum class FileDialog { SaveState, LoadState, ExportBattery, ImportBattery, Palette };
        virtual void showFileDialog(FileDialog dialog) = 0;
        // Show FPS or Show On-Screen Messages changed in the settings.
        virtual void displayChanged() = 0;
        // video.* changed (scale, fullscreen, aspect, ...): the window
        // applies them. `page`: show Settings > Video too.
        virtual void videoChanged(bool showPage) = 0;
        // servers.* changed: the window starts or stops the listeners to
        // match, on its own thread (a call can turn off its own listener).
        virtual void serversChanged() = 0;
        // How the game plays now (speed, fast-forward, rewind): the window
        // sets its frame clock and the sound to match.
        struct Playback {
            double speed = 1.0;      // of the console's rate (Unlimited: 0)
            bool fastForward = false;
            bool rewinding = false;
            bool muted = false;      // the audio policy: stretch up to 2x, then mute
        };
        virtual void playbackChanged(const Playback& playback) = 0;
        // The pacing in use (R4): synced to the display (a frame every
        // divisor-th vsync) or to audio, with the display's refresh rate
        // (0 when unknown) and the frame clock's rate at 100% speed.
        struct PacingNow {
            bool display = false;
            int divisor = 1;
            double displayHz = 0;
            double framesPerSecond = 0;
        };
        virtual PacingNow pacingNow() const = 0;
        // A short confirmation for the user ("State saved to slot 3").
        virtual void showMessage(const QString& text) = 0;
        // select: a path or empty; paused: the command paused a running game.
        virtual void showLibrary(const QString& select, bool paused) = 0;
        virtual void showSettings(const QString& page) = 0;
        // The Key Bindings page (#108) at `section` (`controls` or
        // `hotkeys`); paused: the command paused a running game, which Back
        // resumes.
        virtual void showKeyBindings(const QString& section, bool paused) = 0;
        // The page's model (thread-safe), and a change to it: the window
        // applies it to the menus and the game keys.
        virtual KeyBindings& keyBindings() = 0;
        virtual void bindingsChanged() = 0;
        // The pads plugged in now, by player (thread-safe).
        struct Gamepad {
            int port = 1;
            QString name;
            QString guid;
        };
        virtual QVector<Gamepad> gamepads() const = 0;
        // input.* changed: the window applies the turbo rate and the options.
        virtual void inputSettingsChanged() = 0;
        // audio.* or emulation.* changed: the window applies them.
        virtual void audioSettingsChanged() = 0;
        // The game just opened has an automatic state (emulation.resumeOnOpen):
        // the window asks whether to resume it. The game waits, paused.
        virtual void offerResume(const QString& autoState) = 0;
        // Record Audio started (`path`) or stopped (empty).
        virtual void recordingChanged(const QString& path) = 0;
        // Tools > Cheats… (search: Cheat Search…), and the list changed.
        virtual void showCheats(bool search) = 0;
        virtual void cheatsChanged() = 0;
        // Movie > Record… (`from`: power_on or state) or Play…: the file
        // dialog, starting in `folder` (the game's movies folder, or empty).
        virtual void showMovieDialog(bool record, const QString& from, const QString& folder) = 0;
        // A movie started, stopped or changed mode; movieStatus() has it.
        virtual void movieChanged() = 0;
        // Debug > Debugger, and a breakpoint stop, which shows it too. The
        // debugger reads the machine itself.
        virtual void showDebugger() = 0;
        virtual void showMemoryViewer() = 0;
        virtual void debuggerStopped() = 0;
        virtual void showAbout() = 0;
        virtual void openLibraryFolder() = 0;
        virtual void exit() = 0;
        virtual Library library() const = 0;
        virtual QVector<LibraryEntry> libraryEntries() const = 0;
        virtual void gameOpened(const QString& path, const GameIdentity& id, const BatterySaver::Opened& battery) = 0;
        virtual void gameClosed() = 0;
        virtual void recentChanged() = 0;
        virtual void libraryChanged(const Library& library) = 0;
        virtual void romFoldersChanged() = 0;
        virtual void sessionEvent(const api::Event& event) = 0;
    };

    GuiHost(Window& window, AppSettings& settings, BatterySaver& battery);

    // The session hosting these commands, once it exists. Real-time frames
    // then run through runFrame, which keeps the rewind history.
    void attach(api::Session& session);
    void detach();

    // The speeds of Emulation > Speed, in percent (FR-035).
    static const std::vector<int>& speeds();

    // The open game's save-state slots (any thread; empty without a game).
    StateSlots stateSlots() const;
    int selectedSlot() const { return m_selectedSlot.load(); }

    // The movie recording or playing (any thread), for the overlay.
    struct MovieStatus {
        enum class Mode { None, Recording, Playing };
        Mode mode = Mode::None;
        qint64 frame = 0;  // frames recorded or played so far
        qint64 frames = 0; // in the movie
        qint64 rerecords = 0;
        QString path;
    };
    MovieStatus movieStatus() const;

    // The session's sound passes through here (emulation thread), so Record
    // Audio hears exactly what plays; it goes on to `output`.
    IAudioSink* audioTap(IAudioSink* output);

    std::string mode() const override { return "gui"; }
    void registerCommands(api::Registry& registry) override;
    void onSessionEvent(const api::Event& event) override;

private:
    // `resume`: load the automatic state (true), ignore it (false), or ask.
    api::Result openRom(api::CallContext& ctx, const QString& path, std::optional<bool> resume = std::nullopt);
    QString autoStatePath() const; // the open game's, or empty
    api::Result stopRecording();
    api::Result closeGame(api::CallContext& ctx);
    api::Result setLibraryRoot(api::CallContext& ctx, const QString& root);
    api::Result addRomFolder(const QString& path);
    api::Result removeRomFolder(const QString& path);
    api::Result saveState(api::CallContext& ctx, int slot, const QString& name);
    api::Result loadState(api::CallContext& ctx, int slot);
    // Loads `state`, keeping what it replaced for Undo Load State.
    api::Result loadStateBytes(api::CallContext& ctx, const QByteArray& state, bool keepUndo = true);
    api::Result selectSlot(int slot);
    void runFrame(Machine& machine); // each real-time frame, emulation thread
    // A frame's bookkeeping, around its instructions: the rewind history,
    // the movie's input and cheat freezes at its start (false: the movie
    // ended, so no frame runs), the movie's count at its end.
    bool beginFrame(Machine& machine);
    void endFrame();
    // Runs to the end of the frame, stepping when there are breakpoints.
    // false: one stopped it (the game is paused there, mid-frame).
    bool runToFrameEnd(Machine& machine);
    void stopAtBreakpoint(std::uint16_t pc);
    api::Result stepInstructions(api::CallContext& ctx, int count);
    api::Result setRewind(api::CallContext& ctx, bool on);
    api::Result setSpeed(int percent); // 0: Unlimited
    void updatePlayback();
    // Validates `value` for a video.* key, saves it and has the window apply
    // it. Returns {key, value} as saved.
    api::Result setVideo(const std::string& key, const api::Json& value, bool showPage = false);
    // Emulation > Region and View > Palette (G060-G061), and applying both
    // to the core before a game loads.
    api::Result setRegion(api::CallContext& ctx, const api::Json& value);
    api::Result setPalette(api::CallContext& ctx, const QString& name, const QString& path);
    void applyCoreSettings(api::CallContext& ctx);
    // Cheats (emulation thread): Game Genie codes to the core, a search
    // snapshot, and the game's file.
    void applyGameGenie(Machine& machine);
    static CheatSearch::Snapshot snapshot(Machine& machine);
    api::Result saveCheats();
    api::Json cheatsJson() const;
    // Movies (FR-052), emulation thread. A playing movie sets the input of
    // each frame and refuses what would leave its timeline; a recording one
    // keeps each frame's input and the reset and power buttons.
    api::Result recordMovie(api::CallContext& ctx, const QString& path, bool fromPowerOn);
    api::Result playMovie(api::CallContext& ctx, const QString& path);
    api::Result stopMovie(bool finished = false);
    api::Result takeOverMovie();
    // Fails while a movie plays, and while one records too when `recording`:
    // for the commands that would leave its timeline.
    api::Result movieBusy(bool recording) const;
    bool playMovieFrame(Machine& machine); // false: the movie ended, no frame runs
    void publishMovie(bool changed);
    api::Json movieJson() const;

    Window& m_window;
    AppSettings& m_settings;
    BatterySaver& m_battery;
    std::atomic<api::Session*> m_session{nullptr}; // set on the GUI thread, read on the emulation thread
    bool m_gameOpen = false; // emulation thread
    GameIdentity m_identity; // emulation thread
    CheatList m_cheats;      // emulation thread: the open game's
    QString m_cheatsPath;    // emulation thread: its cheats/<game>.json, or empty
    CheatSearch m_search;    // emulation thread
    QString m_gameName;      // emulation thread: the ROM's file name, for file names
    mutable std::mutex m_slotsMutex;
    StateSlots m_slots; // under m_slotsMutex
    std::atomic<int> m_selectedSlot{1};
    std::optional<QByteArray> m_undoState; // emulation thread; one level (data-model "StateSlot")

    // Emulation thread: the debugger's breakpoints (FR-060). A breakpoint
    // stops before the instruction at its address; Run goes on from one
    // without stopping there again (m_skipBreakpoint). m_midFrame: the
    // frame on screen has begun (a stop or a step part way through it).
    std::bitset<0x10000> m_breakpoints;
    std::optional<std::uint16_t> m_skipBreakpoint;
    bool m_midFrame = false;

    // Emulation thread: the movie (FR-052), with a copy for the window.
    MovieStatus::Mode m_movieMode = MovieStatus::Mode::None;
    Movie m_movie;
    QString m_moviePath;
    qint64 m_movieFrame = 0;
    mutable std::mutex m_movieMutex;
    MovieStatus m_movieStatus; // under m_movieMutex

    // Emulation thread: rewind (R6) and speed (FR-035).
    RewindBuffer m_rewind;
    bool m_rewinding = false;
    bool m_pauseAfterRewind = false;      // rewind started from a paused game
    std::optional<RewindBuffer::Entry> m_rewound; // the state shown last while rewinding
    int m_speed = 100;                    // percent; 0 is Unlimited
    bool m_fastForward = false;
    int m_fastForwardSpeed = 300;         // data-model "emulation.fastForwardSpeed"

    // Record Audio (emulation thread, like the samples).
    struct Tap final : IAudioSink {
        void onSamples(const std::int16_t* pcm, std::size_t count, std::uint64_t start) override
        {
            if (wav.isOpen()) wav.write(pcm, count);
            if (output) output->onSamples(pcm, count, start);
        }
        IAudioSink* output = nullptr;
        WavWriter wav;
    };
    Tap m_tap;
};

} // namespace cutenes::app
