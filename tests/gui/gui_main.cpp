// The gui tests' main: a QApplication on the offscreen platform and SDL's
// dummy audio driver, unless the environment chose others.

#include <QApplication>

#include <gtest/gtest.h>

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    if (qEnvironmentVariableIsEmpty("SDL_AUDIO_DRIVER")) qputenv("SDL_AUDIO_DRIVER", "dummy");
    testing::InitGoogleTest(&argc, argv);
    QApplication app(argc, argv);
    return RUN_ALL_TESTS();
}
