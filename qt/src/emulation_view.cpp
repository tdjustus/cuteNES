#include <cutenes/qt/emulation_view.hpp>

#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QRhiWidget>
#include <QVBoxLayout>
#include <rhi/qrhi.h>

#include <array>
#include <memory>

namespace cutenes::qt {

namespace {

constexpr int kWidth = 256;
constexpr int kHeight = 240;

// The quad's corners for a viewport in an area of `size` device pixels:
// x, y in NDC (y up), then u, v (v down, row 0 at the top).
std::array<float, 16> quadFor(const Viewport& vp, QSize size)
{
    const auto ndcX = [&](double x) { return static_cast<float>(2.0 * x / size.width() - 1.0); };
    const auto ndcY = [&](double y) { return static_cast<float>(1.0 - 2.0 * y / size.height()); };
    const auto u = [](int x) { return static_cast<float>(x) / kWidth; };
    const auto v = [](int y) { return static_cast<float>(y) / kHeight; };
    const QRectF& t = vp.target;
    const QRect& s = vp.source;
    const int right = s.x() + s.width();
    const int bottom = s.y() + s.height();
    // A triangle strip: top-left, bottom-left, top-right, bottom-right.
    return {ndcX(t.left()), ndcY(t.top()), u(s.x()), v(s.y()),
        ndcX(t.left()), ndcY(t.bottom()), u(s.x()), v(bottom),
        ndcX(t.right()), ndcY(t.top()), u(right), v(s.y()),
        ndcX(t.right()), ndcY(t.bottom()), u(right), v(bottom)};
}

class RasterCanvas final : public QWidget {
public:
    explicit RasterCanvas(EmulationView* view) : QWidget(view), m_view(view)
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const bool fresh = m_view->takeFrame();
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        const auto& pixels = m_view->shownPixels();
        if (!pixels.empty()) {
            // Format_RGB32 is 0xffRRGGBB; the core leaves the top byte 0,
            // which QPainter treats as opaque for this format.
            const QImage image(reinterpret_cast<const uchar*>(pixels.data()), kWidth, kHeight,
                kWidth * 4, QImage::Format_RGB32);
            const double dpr = devicePixelRatioF();
            const Viewport vp = computeViewport(QSizeF(size()) * dpr, m_view->displayOptions());
            const QRectF target(vp.target.topLeft() / dpr, vp.target.size() / dpr);
            painter.drawImage(target, image, vp.source);
        }
        if (fresh) {
            emit m_view->frameShown(m_view->shownFrameIndex());
        }
    }

private:
    EmulationView* m_view;
};

class RhiCanvas final : public QRhiWidget {
public:
    explicit RhiCanvas(EmulationView* view) : QRhiWidget(view), m_view(view) {}

protected:
    void initialize(QRhiCommandBuffer*) override
    {
        if (m_rhi != rhi()) {
            m_pipeline.reset();
            m_rhi = rhi();
        }
        if (m_pipeline) {
            return;
        }
        m_texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(kWidth, kHeight)));
        m_texture->create();
        m_sampler.reset(m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest,
            QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        m_sampler->create();
        m_vertices.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
            sizeof(float) * 16));
        m_vertices->create();
        m_uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
        m_uniforms->create();

        m_bindings.reset(m_rhi->newShaderResourceBindings());
        m_bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
                m_uniforms.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                m_texture.get(), m_sampler.get()),
        });
        m_bindings->create();

        m_pipeline.reset(m_rhi->newGraphicsPipeline());
        m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, shader(QStringLiteral(":/cutenes/shaders/view.vert.qsb"))},
            {QRhiShaderStage::Fragment, shader(QStringLiteral(":/cutenes/shaders/view.frag.qsb"))},
        });
        QRhiVertexInputLayout layout;
        layout.setBindings({{4 * sizeof(float)}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
            {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)}});
        m_pipeline->setVertexInputLayout(layout);
        m_pipeline->setSampleCount(sampleCount());
        m_pipeline->setShaderResourceBindings(m_bindings.get());
        m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        m_pipeline->create();
        m_uploaded = false; // a new texture
    }

    void render(QRhiCommandBuffer* cb) override
    {
        QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
        const bool fresh = m_view->takeFrame();
        const auto& pixels = m_view->shownPixels();
        if (!pixels.empty() && (fresh || !m_uploaded)) {
            QRhiTextureSubresourceUploadDescription frame(pixels.data(),
                static_cast<quint32>(pixels.size() * sizeof(std::uint32_t)));
            updates->uploadTexture(m_texture.get(), QRhiTextureUploadDescription({0, 0, frame}));
            m_uploaded = true;
        }
        const QSize size = renderTarget()->pixelSize();
        const auto quad = quadFor(computeViewport(QSizeF(size), m_view->displayOptions()), size);
        updates->updateDynamicBuffer(m_vertices.get(), 0, sizeof(quad), quad.data());
        const QMatrix4x4 corr = m_rhi->clipSpaceCorrMatrix();
        updates->updateDynamicBuffer(m_uniforms.get(), 0, 64, corr.constData());

        cb->beginPass(renderTarget(), Qt::black, {1.0f, 0}, updates);
        if (!pixels.empty() && m_uploaded) {
            cb->setGraphicsPipeline(m_pipeline.get());
            cb->setViewport(QRhiViewport(0, 0, static_cast<float>(size.width()),
                static_cast<float>(size.height())));
            cb->setShaderResources();
            const QRhiCommandBuffer::VertexInput input(m_vertices.get(), 0);
            cb->setVertexInput(0, 1, &input);
            cb->draw(4);
        }
        cb->endPass();
        if (fresh) {
            emit m_view->frameShown(m_view->shownFrameIndex());
        }
    }

    void releaseResources() override
    {
        m_pipeline.reset();
        m_bindings.reset();
        m_uniforms.reset();
        m_vertices.reset();
        m_sampler.reset();
        m_texture.reset();
        m_rhi = nullptr;
    }

private:
    static QShader shader(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader();
    }

    EmulationView* m_view;
    QRhi* m_rhi = nullptr;
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiBuffer> m_vertices;
    std::unique_ptr<QRhiBuffer> m_uniforms;
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    bool m_uploaded = false;
};

bool rasterRequested()
{
    const QString platform = QGuiApplication::platformName();
    return qEnvironmentVariable("CUTENES_RENDERER") == QLatin1String("raster")
        || platform == QLatin1String("offscreen") || platform == QLatin1String("minimal");
}

} // namespace

EmulationView::EmulationView(QWidget* parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    setLayout(new QVBoxLayout);
    layout()->setContentsMargins(0, 0, 0, 0);

    if (rasterRequested()) {
        useRaster();
        return;
    }
    auto* canvas = new RhiCanvas(this);
    // No GPU path at all (a broken driver, a remote desktop): draw with
    // QPainter instead (R2).
    connect(canvas, &QRhiWidget::renderFailed, this, &EmulationView::useRaster, Qt::QueuedConnection);
    m_canvas = canvas;
    m_renderer = Renderer::Rhi;
    layout()->addWidget(m_canvas);
}

EmulationView::~EmulationView() = default;

void EmulationView::useRaster()
{
    if (m_canvas && m_renderer == Renderer::Raster) {
        return;
    }
    if (m_canvas) {
        m_canvas->deleteLater();
    }
    m_canvas = new RasterCanvas(this);
    m_renderer = Renderer::Raster;
    layout()->addWidget(m_canvas);
    m_canvas->show();
    emit rendererChanged();
}

void EmulationView::onFrame(const VideoFrame& frame)
{
    if (!frame.pixels) {
        return;
    }
    m_mailbox.publish(frame.pixels, frame.frameIndex);
    scheduleUpdate();
}

void EmulationView::scheduleUpdate()
{
    // One queued repaint at a time: a GUI thread that falls behind skips
    // frames instead of piling up events.
    if (!m_updatePending.exchange(true)) {
        QMetaObject::invokeMethod(this, [this] {
            m_updatePending = false;
            if (m_canvas) {
                m_canvas->update();
            }
        }, Qt::QueuedConnection);
    }
}

bool EmulationView::takeFrame()
{
    return m_mailbox.takeNewer(m_shown, m_seen, m_shownIndex) && !m_shown.empty();
}

void EmulationView::clearFrame()
{
    m_mailbox.clear();
    scheduleUpdate();
}

void EmulationView::setDisplayOptions(const DisplayOptions& options)
{
    m_options = options;
    updateGeometry();
    if (m_canvas) {
        m_canvas->update();
    }
}

QSize EmulationView::sizeHint() const
{
    return scaledSize(2, m_options).toSize();
}

} // namespace cutenes::qt
