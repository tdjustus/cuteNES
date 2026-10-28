#pragma once

#include "cutenes/bus/bus.hpp"

#include <cutenes/cutenes.hpp>

#include <cstdint>
#include <memory>

// cart.hpp includes this header (its unique_ptr<Mapper> needs the complete
// type), so Cartridge can only be forward-declared here.

namespace cutenes {

class StateReader;
class StateWriter;

struct Cartridge;

// Board/mapper behavior: CPU-bus cartridge decoding (bank switching), PPU
// pattern-bus decoding, and mirroring control (research.md D1 component
// design). Save-state serialization hooks arrive with US5 (task T049).
class Mapper : public CpuBusDevice {
public:
    explicit Mapper(Cartridge& cart)
        : m_cart(cart)
    {
    }
    ~Mapper() override = default;

    // PPU pattern bus $0000-$1FFF; default: direct CHR/CHR-RAM.
    virtual std::uint8_t chrRead(std::uint16_t addr) const;
    virtual void chrWrite(std::uint16_t addr, std::uint8_t value);

    // Nametable mirroring for the PPU; header value unless mapper-controlled.
    virtual Mirroring mirroring() const;

    // Save-state section "MAP " (task T049).
    virtual void saveState(class StateWriter& w) const;
    virtual void loadState(StateReader& r);

    // IRQ generation (MMC3-class): A12-rise clocking from the PPU.
    // Revision override (MMC3-class): manual selection of the IRQ counter
    // rule; the header's NES 2.0 submapper decides by default.
    virtual void setRevisionA(bool) {}
    virtual void clockIrq() {}
    virtual bool irqLine() const { return false; }

    // PRG-RAM write gate ($6000-$7FFF); boards with a protect bit override.
    virtual bool prgRamWritable() const { return true; }
    // PRG-RAM read gate: false = the RAM is disconnected (reads return the
    // floating-bus convention, writes dropped). MMC1B's $E000 bit 4.
    virtual bool prgRamReadable() const { return true; }
    // Offset into the PRG-RAM array for a $6000-$7FFF access (`ramSize` > 0).
    // Boards with banked WRAM (MMC1 SXROM) override; the default mirrors.
    virtual std::size_t prgRamOffset(std::uint16_t addr,
        std::size_t ramSize) const
    {
        return addr % ramSize;
    }

    // --- Mapper-owned low CPU space ($4020-$7FFF) ---------------------------
    // MMC5 decodes its registers at $5000-$5FFF, ExRAM at $5C00-$5FFF and a
    // banked 64K PRG-RAM at $6000-$7FFF. Boards that claim the range take
    // over the Cartridge's generic PRG-RAM window completely.
    virtual bool ownsLowSpace() const { return false; }

    // --- Nametable source ($2000-$2FFF) -----------------------------------
    // MMC5's $5105 maps each 1K nametable slot to CIRAM page 0/1, ExRAM or
    // fill mode. Boards that do not override use the header mirroring.
    enum class NtSource : std::uint8_t { Default, Ciram0, Ciram1, Mapper };
    virtual NtSource ntSource(std::uint16_t /*addr*/) const
    {
        return NtSource::Default;
    }
    virtual std::uint8_t ntRead(std::uint16_t /*addr*/) const { return 0; }
    virtual void ntWrite(std::uint16_t /*addr*/, std::uint8_t /*value*/) {}

    // --- PPU bus context ---------------------------------------------------
    // The PPU reports $2000/$2001 writes so MMC5 can gate substitutions on
    // the PPUCTRL sprite size and the PPUMASK E bits (fully decoded
    // registers; the bus has already folded the mirrors).
    virtual void setPpuCtrl(std::uint8_t /*value*/) {}
    virtual void setPpuMask(std::uint8_t /*value*/) {}

    // Pattern-bus read for a rendering fetch. `sprite` distinguishes sprite
    // pattern fetches (MMC5 8x16 independent banks) from background ones.
    virtual std::uint8_t chrReadBus(std::uint16_t addr, bool /*sprite*/) const
    {
        return chrRead(addr);
    }
    // Direct CHR storage read at a final byte address (no bank mapping):
    // used when a mapper supplies an absolute pattern address (MMC5
    // extended attributes / vertical split).
    std::uint8_t chrReadRaw(std::uint32_t addr) const;

    // Background fetch substitution. The PPU calls once per NT byte, once
    // per attribute byte and once per pattern fetch pair, with the current
    // fetch column (0-33; 0/1 are the next line's prefetches), the line the
    // fetch is for, the address latch and PPUMASK. Return true when the
    // mapper supplied `value` (NT/AT) or replaced `addr` (pattern).
    struct BgFetch {
        int tileColumn = 0;
        int scanline = 0;
        std::uint16_t v = 0;
        std::uint8_t mask = 0;
        bool attribute = false;
        bool pattern = false;
        std::uint8_t value = 0; // NT/AT result
        std::uint32_t addr = 0; // pattern address in/out
    };
    virtual bool bgFetch(BgFetch& /*fetch*/) const { return false; }

    // One call per scanline at dot 0 (MMC5's in-frame/scanline counter).
    virtual void onScanline(int /*line*/, bool /*rendering*/) {}

    // Write-cycle context (MMC1-class consecutive-write rules, T053): the
    // machine wires the scheduler after load; mappers that must know the
    // cycle spacing of back-to-back writes (RMW double writes) override.
    virtual void attachScheduler(class Scheduler& scheduler)
    {
        (void)scheduler;
    }

protected:
    Cartridge& m_cart;
};

// Factory: returns nullptr for mapper ids without an implementation yet
// (mapper 0 here; 1-4 land with US6, tasks T053-T056).
std::unique_ptr<Mapper> createMapper(std::uint16_t mapperId,
    Cartridge& cart);

} // namespace cutenes
