#pragma once

// The game picture (spec FR-040, research R2). It is an IVideoSink: the
// emulation thread hands it each frame through a mailbox, and the GUI
// thread draws the newest one. It renders with QRhiWidget (Metal, Direct3D,
// Vulkan or OpenGL) and falls back to QPainter when no GPU path works, on
// the offscreen platform, or when CUTENES_RENDERER=raster.

#include <cutenes/media.hpp>
#include <cutenes/qt/frame_mailbox.hpp>
#include <cutenes/qt/viewport.hpp>

#include <QWidget>

#include <atomic>
#include <cstdint>
#include <vector>

namespace cutenes::qt {

class EmulationView final : public QWidget, public IVideoSink {
    Q_OBJECT

public:
    enum class Renderer { Rhi, Raster };

    explicit EmulationView(QWidget* parent = nullptr);
    ~EmulationView() override;

    // Any thread (the session's emulation thread).
    void onFrame(const VideoFrame& frame) override;

    // Back to black (the game closed).
    void clearFrame();

    void setDisplayOptions(const DisplayOptions& options);
    DisplayOptions displayOptions() const { return m_options; }
    Renderer renderer() const { return m_renderer; }

    // The frame drawn last and its index (GUI thread). Empty before the
    // first frame.
    const std::vector<std::uint32_t>& shownPixels() const { return m_shown; }
    std::uint64_t shownFrameIndex() const { return m_shownIndex; }

    QSize sizeHint() const override;

    // Canvas side (GUI thread): copies a newer frame into shownPixels().
    // Returns whether there was one.
    bool takeFrame();
    FrameMailbox& mailbox() { return m_mailbox; }

signals:
    void frameShown(quint64 frameIndex);
    void rendererChanged();

private:
    void useRaster();
    void scheduleUpdate();

    FrameMailbox m_mailbox;
    std::atomic<bool> m_updatePending{false};
    DisplayOptions m_options;
    Renderer m_renderer = Renderer::Raster;
    QWidget* m_canvas = nullptr;
    std::vector<std::uint32_t> m_shown;
    std::uint64_t m_shownIndex = 0;
    std::uint64_t m_seen = 0;
};

} // namespace cutenes::qt
