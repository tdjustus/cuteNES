#include "cutenes/system/machine_impl.hpp"

#include <cutenes/cutenes.hpp>

#include <gtest/gtest.h>

namespace {

// Two-port machine harness probing $4016/$4017 through the bus.
struct CtrlHarness {
    cutenes::Scheduler scheduler{cutenes::regionProfile(cutenes::Region::Ntsc)};
    cutenes::Bus bus;
    cutenes::Ppu ppu{cutenes::regionProfile(cutenes::Region::Ntsc), scheduler};
    cutenes::Apu apu{cutenes::regionProfile(cutenes::Region::Ntsc), scheduler,
        bus};
    cutenes::ApuIoBridge io;
    cutenes::ButtonSet controllers[2]{};

    CtrlHarness()
    {
        io.sched = &scheduler;
        io.bus = &bus;
        io.ppu = &ppu;
        io.apu = &apu;
        io.controllers = controllers;
        bus.attachPpu(&ppu);
        bus.attachApuIo(&io);
    }

    std::uint8_t read(int port)
    {
        return bus.read(port == 0 ? 0x4016 : 0x4017);
    }

    void strobe()
    {
        bus.write(0x4016, 1);
        bus.write(0x4016, 0);
    }
};

} // namespace

TEST(Controllers, SerialOrderIsASelStartUDLR)
{
    CtrlHarness h;
    cutenes::ButtonSet set;
    set[static_cast<std::size_t>(cutenes::Button::A)] = 1;
    set[static_cast<std::size_t>(cutenes::Button::Select)] = 1;
    set[static_cast<std::size_t>(cutenes::Button::Down)] = 1;
    h.controllers[0] = set;

    h.strobe();
    // Read order: A, B, Select, Start, Up, Down, Left, Right.
    const int expected[8] = {1, 0, 1, 0, 0, 1, 0, 0};
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(h.read(0) & 1, expected[i]) << "read " << i;
    }
}

TEST(Controllers, ReadsAfterEightReturnOne)
{
    // After 8 shifts the official controller keeps returning 1s (nesdev
    // Controllers; terminates blargg dmc_dma_during_read4's bit-count
    // loop — previously modeled as 0, which hung that ROM).
    CtrlHarness h;
    h.strobe();
    for (int i = 0; i < 8; ++i) {
        (void)h.read(0);
    }
    EXPECT_EQ(h.read(0) & 1, 1);
    EXPECT_EQ(h.read(0) & 1, 1);
}

TEST(Controllers, PortsAreIsolated)
{
    CtrlHarness h;
    cutenes::ButtonSet p1;
    p1[static_cast<std::size_t>(cutenes::Button::B)] = 1;
    cutenes::ButtonSet p2;
    p2[static_cast<std::size_t>(cutenes::Button::Right)] = 1;
    h.controllers[0] = p1;
    h.controllers[1] = p2;

    h.strobe();
    EXPECT_EQ(h.read(0) & 1, 0); // port 1: A first = 0, B = 1 second
    EXPECT_EQ(h.read(0) & 1, 1);
    EXPECT_EQ(h.read(1) & 1, 0); // port 2: seven zeros then Right = 1
    for (int i = 0; i < 6; ++i) {
        (void)h.read(1);
    }
    EXPECT_EQ(h.read(1) & 1, 1); // 8th read = Right
}

TEST(Controllers, StrobeHeldReturnsButtonA)
{
    CtrlHarness h;
    cutenes::ButtonSet set;
    set[static_cast<std::size_t>(cutenes::Button::A)] = 1;
    h.controllers[0] = set;

    h.bus.write(0x4016, 1); // strobe held
    EXPECT_EQ(h.read(0) & 1, 1);
    EXPECT_EQ(h.read(0) & 1, 1); // does not advance while held
    h.bus.write(0x4016, 0); // falling edge restarts the shift
    EXPECT_EQ(h.read(0) & 1, 1);
    EXPECT_EQ(h.read(0) & 1, 0);
}
