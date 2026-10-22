# UI Design Plan: Embeddable Qt Emulation View

**Feature**: 001-emulation-core  
**Status**: Design for Future Implementation  
**Purpose**: Define architecture for a reusable Qt-based emulation view that can be embedded in any Qt application

## Goals

1. **Strict Core/UI Separation** - The emulation view must only interact with the core through the public API (`cutenes::Machine`, `IVideoSink`, `IAudioSink`, etc.). No core internals or implementation details.
2. **Embeddable by Design** - The widget must be usable as a drop-in component in any Qt 6 application without assumptions about application structure, menus, or lifecycle.
3. **Library-First Architecture** - Ship as a separate `cutenes-qt` library with its own CMake targets, install rules, and find_package support.
4. **Reusable Across Contexts** - Same widget works in a full emulator app, a debugging tool, a level editor, a TAS tool, or any custom Qt application.
5. **Performance** - Hardware-accelerated rendering with minimal latency, proper audio synchronization.

## Architecture Principles

### 1. Core Separation
- All emulation logic lives in `cutenes-core` (zero Qt dependencies)
- UI layer only consumes the public API defined in `specs/001-emulation-core/contracts/core-api.md`
- Use dependency injection for all core objects (no singletons)
- Widget never reaches into core's private headers

### 2. Interface-Driven Design
```cpp
// The view implements/adapts core interfaces
class VideoWidget : public QWidget, public IVideoSink {
    void onFrame(const VideoFrame& frame) override;
};

class AudioBackend : public QObject, public IAudioSink {
    void onSamples(const std::int16_t* pcm, std::size_t count, 
                   std::uint64_t startMasterCycle) override;
};
```

### 3. PIMPL for ABI Stability
All public classes use the Pointer to Implementation pattern to maintain ABI stability across Qt/cutenes versions and allow internal changes without breaking dependent applications.

### 4. No Global State
- No static instances of Machine or emulation state
- All configuration passed explicitly
- Multiple EmulationView instances can coexist (e.g., split-screen debugging)
- Thread-safe API boundaries (core is single-threaded; UI marshals to core thread)

## Component Design

### EmulationView (Main Widget)
The primary embeddable component. Acts as the coordinator between core and subsystems.

```cpp
namespace cutenes::qt {

enum class ScalingMode {
    Original,      // 1x pixel size
    Integer,       // Nearest integer multiple
    Fit,           // Fit to widget, maintain aspect ratio (letterbox/pillarbox)
    Fill,          // Fill widget, maintain aspect ratio (crop)
    Stretch        // Stretch to fill (ignore aspect ratio)
};

enum class FilterType {
    Nearest,       // Pixelated, sharp
    Bilinear,      // Smooth
    CRT,           // CRT shader effect (optional)
    NTSC           // NTSC composite simulation (optional)
};

class CUTENES_QT_API EmulationView : public QWidget {
    Q_OBJECT

public:
    explicit EmulationView(QWidget* parent = nullptr);
    ~EmulationView() override;

    // Machine integration (non-owning by default)
    void setMachine(cutenes::Machine* machine);
    void setMachine(std::shared_ptr<cutenes::Machine> machine);
    cutenes::Machine* machine() const;
    
    // Display configuration
    void setScalingMode(ScalingMode mode);
    ScalingMode scalingMode() const;
    void setFilter(FilterType filter);
    FilterType filter() const;
    void setShowOverlay(bool show); // FPS, frame counter, etc.
    
    // Input configuration
    void setControllerPort(int port, bool enabled);
    void setInputMapper(std::unique_ptr<IInputMapper> mapper);
    
    // Audio
    void setAudioEnabled(bool enabled);
    bool audioEnabled() const;
    void setAudioVolume(float volume); // 0.0 to 1.0
    
    // Control (delegates to machine)
    void powerOn();
    void reset();
    void pause();
    void resume();
    bool isPaused() const;
    
    // Sizing
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    int heightForWidth(int w) const override;
    bool hasHeightForWidth() const override;

signals:
    void machineAttached(cutenes::Machine* machine);
    void frameRendered(std::uint64_t frameIndex);
    void pausedStateChanged(bool paused);
    void errorOccurred(const QString& message);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    
private:
    class Private;
    std::unique_ptr<Private> d_ptr;
};

} // namespace cutenes::qt
```

### Video Rendering
Two implementation strategies with automatic fallback:

#### Option 1: OpenGL (Preferred)
- Use `QOpenGLWidget` as base or composition
- Upload 256x240 XRGB8888 framebuffer as texture
- Vertex/fragment shaders for scaling and filters
- Efficient, low latency, supports CRT/NTSC shaders
- Works well across platforms

#### Option 2: Raster (Fallback)
- Use `QWidget` with `QImage` conversion
- `QImage` can wrap or copy the core's framebuffer (XRGB8888 compatible)
- Use `QPainter` with transform for scaling
- No GPU dependencies, works on all systems
- Slightly higher CPU usage but acceptable

The implementation should detect OpenGL availability and fall back gracefully.

### Audio Backend
```cpp
namespace cutenes::qt {

class AudioBackend : public QObject, public cutenes::IAudioSink {
    Q_OBJECT
public:
    explicit AudioBackend(QObject* parent = nullptr);
    ~AudioBackend() override;
    
    void setSampleRate(std::uint32_t rate);
    void setVolume(float volume);
    void setEnabled(bool enabled);
    
    // IAudioSink
    void onSamples(const std::int16_t* pcm, std::size_t count,
                   std::uint64_t startMasterCycle) override;
    
private slots:
    void handleAudioStateChanged(QAudio::State state);
    
private:
    class Private;
    std::unique_ptr<Private> d_ptr;
};

} // namespace cutenes::qt
```

Uses `QAudioSink` with a lock-free ring buffer. Handles device changes, volume control, and proper cleanup.

### Input Mapping
```cpp
namespace cutenes::qt {

class IInputMapper {
public:
    virtual ~IInputMapper() = default;
    virtual cutenes::ButtonSet mapKey(int qtKey, bool pressed) = 0;
    virtual cutenes::ButtonSet mapGamepad(const QGamepad* gamepad) = 0;
};

class DefaultInputMapper : public IInputMapper {
    // Maps arrow keys to D-pad, Z/X to B/A, Shift/Enter to Select/Start, etc.
};

} // namespace cutenes::qt
```

Supports keyboard and gamepad (QtGamepad or SDL gamepad via abstraction).

## Embeddability Features

### 1. Flexible Integration
- **As child widget**: `new cutenes::qt::EmulationView(parent)` in any layout
- **As dock widget**: Embed in `QMainWindow` docks
- **As dialog**: Use in modal/non-modal dialogs
- **In QML**: Provide `QQuickItem` wrapper (`EmulationViewItem`)
- **Multiple instances**: Each view manages its own machine/audio/video independently

### 2. No Application Assumptions
- No reliance on global `QApplication` state beyond Qt requirements
- No hardcoded file paths or resource locations
- No assumptions about menu bars, toolbars, or actions
- Accepts ROM data via core API (view doesn't load files directly)
- All user interactions signal out to host application

### 3. Styling & Theming
- Inherits Qt widget palette and style
- Respects system DPI scaling
- No custom stylesheets required
- Can be styled by host application
- Overlay elements use semi-transparent rendering compatible with themes

### 4. Lifecycle Management
- View can outlive machine or vice versa (handles null machine gracefully)
- Proper cleanup on destruction (stops audio, detaches sinks)
- Safe to delete while emulation running
- Thread-safe attachment/detachment

### 5. Extensibility
```cpp
// Host app can extend
class CustomEmulationView : public cutenes::qt::EmulationView {
    // Override paintEvent for custom overlays
    void paintEvent(QPaintEvent* event) override {
        EmulationView::paintEvent(event);
        // Draw custom UI elements
    }
};

// Or use composition
EmulationView* view = new EmulationView(this);
view->setContextMenuPolicy(Qt::CustomContextMenu);
connect(view, &QWidget::customContextMenuRequested, this, &MyApp::showMenu);
```

## CMake Integration

### Library Structure
```
ui/qt/
├── CMakeLists.txt
├── include/cutenes/qt/
│   ├── cutenes_qt_export.h
│   ├── EmulationView.h
│   ├── AudioBackend.h
│   ├── InputMapper.h
│   └── Types.h
├── src/
│   ├── EmulationView.cpp
│   ├── EmulationViewPrivate.h
│   ├── VideoWidget.cpp
│   ├── VideoWidget.h
│   ├── AudioBackend.cpp
│   ├── AudioBackendPrivate.h
│   ├── InputMapper.cpp
│   └── shaders/ (GLSL shaders)
└── resources/ (optional)
```

### CMakeLists.txt (ui/qt)
```cmake
cmake_minimum_required(VERSION 3.21)
project(cutenes-qt LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Multimedia)
find_package(cutenes-core REQUIRED)

add_library(cutenes-qt SHARED)
add_library(cutenes-qt::cutenes-qt ALIAS cutenes-qt)

target_sources(cutenes-qt
    PRIVATE
        src/EmulationView.cpp
        src/VideoWidget.cpp
        src/AudioBackend.cpp
        src/InputMapper.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/cutenes/qt/EmulationView.h
                include/cutenes/qt/AudioBackend.h
                include/cutenes/qt/InputMapper.h
                include/cutenes/qt/Types.h
)

target_link_libraries(cutenes-qt
    PUBLIC
        cutenes-core::cutenes-core
        Qt6::Widgets
    PRIVATE
        Qt6::OpenGLWidgets
        Qt6::Multimedia
)

target_compile_features(cutenes-qt PUBLIC cxx_std_20)
set_target_properties(cutenes-qt PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

include(GenerateExportHeader)
generate_export_header(cutenes-qt
    BASE_NAME CUTENES_QT
    EXPORT_FILE_NAME ${CMAKE_CURRENT_SOURCE_DIR}/include/cutenes/qt/cutenes_qt_export.h
)

install(TARGETS cutenes-qt
    EXPORT cutenes-qtTargets
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
    FILE_SET HEADERS DESTINATION include
)

install(EXPORT cutenes-qtTargets
    FILE cutenes-qtTargets.cmake
    NAMESPACE cutenes-qt::
    DESTINATION lib/cmake/cutenes-qt
)

configure_file(cmake/cutenes-qtConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cutenes-qtConfig.cmake @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cutenes-qtConfig.cmake
    DESTINATION lib/cmake/cutenes-qt)
```

### Usage by external apps
```cmake
# Method 1: find_package
find_package(cutenes-qt REQUIRED)
target_link_libraries(myapp PRIVATE cutenes-qt::cutenes-qt)

# Method 2: subdirectory
add_subdirectory(external/cuteNES/ui/qt)
target_link_libraries(myapp PRIVATE cutenes-qt::cutenes-qt)
```

## Implementation Guidelines

### Performance Considerations
1. **Zero-copy rendering** where possible - Pass core framebuffer directly to GL texture
2. **Avoid conversions** - Use XRGB8888 format matching between core and Qt/GL
3. **Triple buffering** for smooth rendering
4. **Audio ring buffer** sized appropriately (e.g., 4096-8192 samples)
5. **Input polling** on appropriate thread/event loop
6. **VSync control** - allow disabling for benchmarking/TAS

### Threading Model
- Core is single-threaded, called from UI thread or dedicated emulation thread
- `EmulationView` can run emulation in its own thread or integrate with host's timer/event loop
- All core calls must be serialized (use mutex or thread affinity)
- UI updates from emulation thread must use `QMetaObject::invokeMethod` or signals/slots

### Error Handling
- Graceful degradation if OpenGL unavailable
- Audio device errors don't crash emulation
- Invalid machine states handled safely
- Clear error signals to host application

### Platform Considerations
- **Windows**: OpenGL or DirectX via ANGLE, WASAPI via QtMultimedia
- **macOS**: Metal via Qt RHI or OpenGL, CoreAudio
- **Linux**: OpenGL/Vulkan, PulseAudio/ALSA
- Use Qt's cross-platform abstractions, avoid platform-specific code in widget

## Testing Strategy
- Unit tests for input mapping, scaling calculations
- Integration tests with test ROMs (nestest for display)
- Embedding tests in minimal host applications
- Performance benchmarks (frame time, input latency)
- DPI scaling tests
- Multiple instance tests

## Migration Path
Since this is for future implementation:
1. Create the directory structure
2. Add CMake integration points
3. Document API contracts
4. Implement incrementally (raster first, then OpenGL, then QML)
5. Maintain backward compatibility in API design

## References
- [Qt 6 Documentation - Custom Widgets](https://doc.qt.io/qt-6/custom-widgets.html)
- [Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-index.html)
- [cuteNES Core API](./contracts/core-api.md)
- [cuteNES Media Output Contract](./contracts/media-output.md)
- Constitution Principle II (Core/UI Separation)
- Constitution Principle V (Automation Parity)
