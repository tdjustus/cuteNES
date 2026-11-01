// G050, G052-G053 (spec US7-4, US7-5, FR-036, FR-037, FR-042, FR-044;
// data-model "Settings" audio.* and emulation.*): sound and comfort.
// - The audio settings apply to the output and persist; a saved device that
//   is gone falls back to the default with a message, and play goes on.
// - Fast-forward's sound follows emulation.fastForwardSpeed and
//   audio.fastForwardAudio: stretched up to 2x, or muted.
// - Record Audio writes a WAV of the game's sound: a 44-byte PCM header
//   (RIFF, 48000 Hz, mono, 16-bit) and 48000 / 60.0988 samples per frame.
// - The game pauses when the window loses focus (on by default) and
//   resumes when it comes back, unless the player paused it.
// - With emulation.resumeOnOpen on, closing a game keeps an automatic state
//   that opening it again offers, or that `resume` loads.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <cutenes/qt/audio_output.hpp>
#include <cutenes/qt/media_runtime.hpp>

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QMessageBox>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QtEndian>

#include <gtest/gtest.h>

#include <algorithm>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

const QString kRom = QStringLiteral(CUTENES_TEST_ROMS "/instr_test-v5/rom_singles/01-basics.nes");

Json call(MainWindow& w, const char* name, const Json& args = Json::object())
{
    const Result r = w.session().call(name, args);
    EXPECT_FALSE(r.isError()) << name << ": " << (r.isError() ? r.error->message : "");
    return r.value;
}

std::string runState(MainWindow& w)
{
    return call(w, "core.run_state")["runState"].get<std::string>();
}

void openGame(MainWindow& w, const Json& extra = Json::object())
{
    Json args = {{"path", kRom.toStdString()}};
    args.update(extra);
    ASSERT_FALSE(w.session().call("gui.open_rom", args).isError());
    ASSERT_TRUE(waitUntil([&] { return w.windowTitle().contains(QStringLiteral("01-basics.nes")); }));
}

Json audioSettings(MainWindow& w)
{
    return call(w, "gui.get_settings", {{"group", "audio"}})["values"]["audio"];
}

} // namespace

TEST(GuiAudio, TheAudioSettingsApplyAndPersist)
{
    const cutenes::qt::MediaRuntime media; // SDL's dummy driver in the tests
    ASSERT_TRUE(media.audioAvailable()) << media.error();
    QString folder;
    const MainWindow::Options options = windowOptions(&folder);
    {
        MainWindow w(options);
        ASSERT_TRUE(w.audio()->isOpen()) << w.audio()->error();
        EXPECT_EQ(audioSettings(w), Json({{"device", "default"}, {"volume", 100}, {"muted", false},
                                        {"latencyMs", 64}, {"fastForwardAudio", "stretch"}}))
            << audioSettings(w).dump();
        const Json devices = call(w, "gui.list_audio_devices")["devices"];
        ASSERT_FALSE(devices.empty()) << "the dummy driver has a device";

        call(w, "gui.set_setting", {{"key", "audio.volume"}, {"value", 40}});
        call(w, "gui.set_setting", {{"key", "audio.muted"}, {"value", true}});
        call(w, "gui.set_setting", {{"key", "audio.latencyMs"}, {"value", 120}});
        call(w, "gui.set_setting", {{"key", "audio.device"}, {"value", devices[0]}});
        EXPECT_TRUE(waitUntil([&] {
            return w.audio()->volume() == 40 && w.audio()->userMuted() && w.audio()->targetLatencyMs() == 120
                && w.audio()->device() == devices[0].get<std::string>();
        }));
        for (const auto& [key, value] : std::vector<std::pair<const char*, Json>>{{"audio.volume", 101},
                 {"audio.latencyMs", 31}, {"audio.latencyMs", 201}, {"audio.muted", "yes"},
                 {"audio.device", "No Such Device"}, {"audio.fastForwardAudio", "loud"}}) {
            const Result bad = w.session().call("gui.set_setting", {{"key", key}, {"value", value}});
            ASSERT_TRUE(bad.isError()) << key << " " << value.dump();
            EXPECT_EQ(bad.error->data["path"], "/value") << key;
        }
    }
    {
        MainWindow again(options);
        EXPECT_EQ(again.audio()->volume(), 40);
        EXPECT_TRUE(again.audio()->userMuted());
        EXPECT_EQ(again.audio()->targetLatencyMs(), 120);
    }

    // A saved device that is gone: the default plays, and the player is told.
    QSettings(options.settingsFile, QSettings::IniFormat).setValue(QStringLiteral("audio/device"), QStringLiteral("Gone USB DAC"));
    QStringList warnings;
    MainWindow gone(options);
    QObject::connect(&gone, &MainWindow::messageShown, [&](const QString& text) { warnings << text; });
    EXPECT_TRUE(gone.audio()->isOpen());
    EXPECT_EQ(gone.audio()->device(), "") << "the default device";
    EXPECT_TRUE(waitUntil([&] { return !warnings.isEmpty(); }));
    EXPECT_TRUE(!warnings.isEmpty() && warnings.last().contains(QStringLiteral("Gone USB DAC")))
        << warnings.join(QStringLiteral(" | ")).toStdString();
}

TEST(GuiAudio, FastForwardSoundStretchesOrMutes)
{
    MainWindow w(windowOptions());
    openGame(w);
    EXPECT_EQ(call(w, "gui.get_settings", {{"group", "emulation"}})["values"]["emulation"]["fastForwardSpeed"], 300);
    call(w, "gui.set_setting", {{"key", "emulation.fastForwardSpeed"}, {"value", 200}});
    call(w, "gui.set_fast_forward", {{"on", true}});
    EXPECT_TRUE(waitUntil([&] { return w.playback().fastForward && w.playback().speed == 2.0; }));
    EXPECT_FALSE(w.playback().muted) << "2x is stretched";
    call(w, "gui.set_setting", {{"key", "audio.fastForwardAudio"}, {"value", "mute"}});
    EXPECT_TRUE(waitUntil([&] { return w.playback().muted; })) << "muted while fast-forwarding";
    call(w, "gui.set_fast_forward", {{"on", false}});
    EXPECT_TRUE(waitUntil([&] { return !w.playback().muted && w.playback().speed == 1.0; }));
    const Result bad = w.session().call("gui.set_setting", {{"key", "emulation.fastForwardSpeed"}, {"value", 100}});
    ASSERT_TRUE(bad.isError());
    EXPECT_EQ(bad.error->data["path"], "/value");
}

TEST(GuiAudio, RecordAudioWritesAWavOfTheGamesSound)
{
    QString folder;
    MainWindow w(windowOptions(&folder));
    openGame(w);
    call(w, "core.pause");
    const QString path = folder + QStringLiteral("/take.wav");
    const Json started = call(w, "gui.toggle_audio_recording", {{"path", path.toStdString()}});
    EXPECT_EQ(started["recording"], true);
    EXPECT_EQ(started["path"], path.toStdString());
    call(w, "core.run_frames", {{"frames", 600}});
    const Json stopped = call(w, "gui.toggle_audio_recording");
    EXPECT_EQ(stopped["recording"], false);
    EXPECT_EQ(stopped["path"], path.toStdString());

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray wav = file.readAll();
    ASSERT_GE(wav.size(), 44);
    const auto u32 = [&](int at) { return qFromLittleEndian<quint32>(wav.constData() + at); };
    const auto u16 = [&](int at) { return qFromLittleEndian<quint16>(wav.constData() + at); };
    EXPECT_EQ(wav.mid(0, 4), "RIFF");
    EXPECT_EQ(u32(4), quint32(wav.size() - 8));
    EXPECT_EQ(wav.mid(8, 8), "WAVEfmt ");
    EXPECT_EQ(u32(16), 16u);    // the PCM fmt chunk
    EXPECT_EQ(u16(20), 1u);     // PCM
    EXPECT_EQ(u16(22), 1u);     // mono
    EXPECT_EQ(u32(24), 48000u); // samples a second
    EXPECT_EQ(u32(28), 96000u); // bytes a second
    EXPECT_EQ(u16(32), 2u);     // bytes a sample
    EXPECT_EQ(u16(34), 16u);    // bits
    EXPECT_EQ(wav.mid(36, 4), "data");
    EXPECT_EQ(u32(40), quint32(wav.size() - 44));
    // 600 NTSC frames are 600 / 60.0988 s: 479,211 samples.
    const double samples = u32(40) / 2.0;
    EXPECT_NEAR(samples, 600 * 48000 / 60.0988, 48000 * 0.02) << "within 20 ms";

    // From the menu, it goes into the library's recordings folder.
    const Json fromMenu = call(w, "gui.toggle_audio_recording");
    EXPECT_NE(fromMenu["path"].get<std::string>().find("/recordings/"), std::string::npos) << fromMenu.dump();
    call(w, "gui.close_game"); // stops it
    EXPECT_TRUE(QFile::exists(QString::fromStdString(fromMenu["path"].get<std::string>())));
    const Result none = w.session().call("gui.toggle_audio_recording");
    ASSERT_TRUE(none.isError());
    EXPECT_EQ(none.error->name, "NoCartridge");
}

TEST(GuiAudio, TheGamePausesWhileTheWindowIsInactive)
{
    MainWindow w(windowOptions());
    w.show();
    w.activateWindow();
    ASSERT_TRUE(waitUntil([&] { return QApplication::activeWindow() == &w; }));
    openGame(w);
    ASSERT_EQ(runState(w), "Running");
    EXPECT_EQ(call(w, "gui.get_settings", {{"group", "emulation"}})["values"]["emulation"]["pauseWhenInactive"], true);

    QWidget other;
    other.show();
    other.activateWindow();
    ASSERT_TRUE(waitUntil([&] { return QApplication::activeWindow() == &other; }));
    EXPECT_TRUE(waitUntil([&] { return runState(w) == "Paused"; }));
    w.activateWindow();
    ASSERT_TRUE(waitUntil([&] { return QApplication::activeWindow() == &w; }));
    EXPECT_TRUE(waitUntil([&] { return runState(w) == "Running"; })) << "focus back: play on";

    // Paused by the player, it stays paused.
    call(w, "gui.toggle_pause");
    other.activateWindow();
    ASSERT_TRUE(waitUntil([&] { return QApplication::activeWindow() == &other; }));
    w.activateWindow();
    ASSERT_TRUE(waitUntil([&] { return QApplication::activeWindow() == &w; }));
    QApplication::processEvents();
    EXPECT_EQ(runState(w), "Paused");
    call(w, "gui.toggle_pause");

    // Off, it plays on.
    call(w, "gui.set_setting", {{"key", "emulation.pauseWhenInactive"}, {"value", false}});
    other.activateWindow();
    ASSERT_TRUE(waitUntil([&] { return QApplication::activeWindow() == &other; }));
    QApplication::processEvents();
    EXPECT_EQ(runState(w), "Running");
}

TEST(GuiAudio, ResumeWhereYouLeftOff)
{
    QString folder;
    MainWindow w(windowOptions(&folder));
    w.show();
    EXPECT_EQ(call(w, "gui.get_settings", {{"group", "emulation"}})["values"]["emulation"]["resumeOnOpen"], false);

    // Off: closing keeps nothing.
    openGame(w);
    call(w, "gui.close_game");
    openGame(w);
    EXPECT_FALSE(call(w, "gui.list_slots").contains("autoState"));

    call(w, "gui.set_setting", {{"key", "emulation.resumeOnOpen"}, {"value", true}});
    call(w, "core.run_frames", {{"frames", 300}});
    const std::uint64_t left = call(w, "core.counters")["frame"].get<std::uint64_t>();
    ASSERT_GE(left, 300u);
    call(w, "gui.close_game");

    // `resume: false` starts afresh; `resume: true` is where it was left.
    const Json fresh = call(w, "gui.open_rom", {{"path", kRom.toStdString()}, {"resume", false}});
    EXPECT_TRUE(fresh.contains("autoState")) << fresh.dump();
    EXPECT_LT(call(w, "core.counters")["frame"].get<std::uint64_t>(), left);
    call(w, "gui.close_game"); // keeps the fresh run's state now
    call(w, "gui.open_rom", {{"path", kRom.toStdString()}, {"resume", false}});
    call(w, "core.pause");
    call(w, "core.run_frames", {{"frames", 400}});
    const std::uint64_t later = call(w, "core.counters")["frame"].get<std::uint64_t>();
    call(w, "gui.close_game");
    const Json resumed = call(w, "gui.open_rom", {{"path", kRom.toStdString()}, {"resume", true}});
    EXPECT_EQ(resumed["resumed"], true);
    EXPECT_GE(call(w, "core.counters")["frame"].get<std::uint64_t>(), later);

    // Without `resume`, the window asks; Yes resumes.
    call(w, "gui.close_game");
    ASSERT_FALSE(w.session().call("gui.open_rom", {{"path", kRom.toStdString()}}).isError());
    QMessageBox* ask = nullptr;
    ASSERT_TRUE(waitUntil([&] { return (ask = w.findChild<QMessageBox*>(QStringLiteral("resumeOffer"))) != nullptr; }));
    ask->button(QMessageBox::Yes)->click();
    EXPECT_TRUE(waitUntil([&] { return call(w, "core.counters")["frame"].get<std::uint64_t>() >= later; }));
    EXPECT_TRUE(waitUntil([&] { return runState(w) == "Running"; }));
}

TEST(GuiAudio, TheAudioAndEmulationPagesHoldTheSettings)
{
    MainWindow w(windowOptions());
    EXPECT_EQ(call(w, "gui.show_settings", {{"page", "audio"}})["page"], "audio");
    QWidget* dialog = nullptr;
    ASSERT_TRUE(waitUntil([&] {
        return (dialog = w.findChild<QWidget*>(QStringLiteral("settingsDialog"))) && dialog->isVisible();
    }));
    auto* device = dialog->findChild<QComboBox*>(QStringLiteral("audioDevice"));
    auto* volume = dialog->findChild<QSlider*>(QStringLiteral("volume"));
    auto* muted = dialog->findChild<QCheckBox*>(QStringLiteral("muted"));
    auto* latency = dialog->findChild<QSpinBox*>(QStringLiteral("latencyMs"));
    auto* ffAudio = dialog->findChild<QComboBox*>(QStringLiteral("fastForwardAudio"));
    auto* inactive = dialog->findChild<QCheckBox*>(QStringLiteral("pauseWhenInactive"));
    auto* resume = dialog->findChild<QCheckBox*>(QStringLiteral("resumeOnOpen"));
    auto* ffSpeed = dialog->findChild<QComboBox*>(QStringLiteral("fastForwardSpeed"));
    ASSERT_TRUE(device && volume && muted && latency && ffAudio && inactive && resume && ffSpeed);
    EXPECT_EQ(device->currentData().toString(), QStringLiteral("default"));
    EXPECT_EQ(volume->value(), 100);
    EXPECT_EQ(latency->value(), 64);
    EXPECT_TRUE(inactive->isChecked());
    EXPECT_FALSE(resume->isChecked());
    EXPECT_EQ(ffSpeed->currentData().toInt(), 300);

    volume->setValue(25);
    muted->click();
    resume->click();
    ffSpeed->setCurrentIndex(ffSpeed->findData(400));
    EXPECT_TRUE(waitUntil([&] {
        const Json v = call(w, "gui.get_settings")["values"];
        return v["audio"]["volume"] == 25 && v["audio"]["muted"] == true && v["emulation"]["resumeOnOpen"] == true
            && v["emulation"]["fastForwardSpeed"] == 400;
    })) << call(w, "gui.get_settings")["values"].dump();
    EXPECT_EQ(call(w, "gui.show_settings", {{"page", "emulation"}})["page"], "emulation");
}
