// MMC3 (mapper 4) — task T056.
//
// PRG: 8K windows — $8000/$C000 = R6 and second-last swapped by mode bit 6,
// $A000 = R7, $E000 = last bank (fixed). CHR: 2Kx4 or 1Kx8 by mode bit 7. A12-rise IRQ
// clocking (PPU-driven); IRQ counter with reload/enable/acknowledge.
// Battery WRAM with protect bit.

#include "cutenes/mappers/mapper.hpp"

#include "cutenes/cartridge/cart.hpp"
#include "cutenes/serialize/save_state.hpp"

namespace cutenes {

namespace {

class Mmc3 final : public Mapper {
public:
    explicit Mmc3(Cartridge& cartridge)
        : Mapper(cartridge)
    {
        // Power-on: R7 selects the last bank so the reset vector is
        // reachable (universal emulator convention).
        const std::size_t banks = cartridge.prg.size() / 0x2000;
        mPrg[1] = static_cast<std::uint8_t>(banks > 0 ? banks - 1 : 0);
        // NES 2.0 mapper 4 submapper 4 selects the MMC3 revision A
        // ("alternate") IRQ rule; submapper 0 / plain iNES is revision B
        // ("normal") — nesdev wiki MMC3, IRQ Specifics.
        mRevA = cartridge.info.mapperId == 4 && cartridge.info.submapper == 4;
    }

    std::uint8_t cpuRead(std::uint16_t addr) override
    {
        if (addr < 0x8000) {
            return m_cart.prgRam.empty()
                ? 0
                : m_cart.prgRam[addr % m_cart.prgRam.size()];
        }
        const std::size_t banks = m_cart.prg.size() / 0x2000;
        const auto bankOf = [&](std::size_t reg) -> std::size_t {
            return static_cast<std::size_t>(mPrg[reg])
                & (banks > 0 ? banks - 1 : 0);
        };
        // PRG mode (bit 6): 0 => $8000=R6, $C000=second-last;
        // 1 => $8000=second-last, $C000=R6. $A000=R7; $E000=last (fixed).
        const bool mode1 = (mBankMode & 0x40) != 0;
        const std::size_t last = banks > 0 ? banks - 1 : 0;
        const std::size_t second = banks > 1 ? banks - 2 : 0;
        std::size_t bank;
        if (addr < 0xA000) {
            bank = mode1 ? second : bankOf(0);
        } else if (addr < 0xC000) {
            bank = bankOf(1);
        } else if (addr < 0xE000) {
            bank = mode1 ? bankOf(0) : second;
        } else {
            bank = last;
        }
        return m_cart.prg[(bank * 0x2000 + (addr & 0x1FFF))
            % m_cart.prg.size()];
    }

    void cpuWrite(std::uint16_t addr, std::uint8_t value) override
    {
        if (addr < 0x8000) {
            if (!m_cart.prgRam.empty() && (mWramProtect & 0x80) == 0) {
                m_cart.prgRam[addr % m_cart.prgRam.size()] = value;
            }
            return;
        }
        const bool even = (addr & 1) == 0;
        switch ((addr - 0x8000) >> 13) {
        case 0: // $8000/$8001
            if (even) {
                mBankMode = value;
            } else {
                // The register index comes from the earlier $8000 write.
                const std::uint8_t reg = static_cast<std::uint8_t>(
                    mBankMode & 7);
                if (reg < 6) {
                    mChr[reg] = value;
                } else {
                    mPrg[reg - 6] = static_cast<std::uint8_t>(
                        value & 0x3F);
                }
            }
            break;
        case 1: // $A000/$A001
            if (even) {
                mMirroring = value;
            } else {
                mWramProtect = value;
            }
            break;
        case 2: // $C000/$C001
            if (even) {
                mIrqLatch = value;
            } else {
                // $C001 sets the counter above 127 and arms the reload
                // (nesdev: the instant OR keeps the counter from reaching
                // 0 on its own before the reload clock).
                mIrqCounter = static_cast<std::uint8_t>(mIrqCounter | 0x80);
                mReloadPending = true;
            }
            break;
        default: // $E000/$E001
            mIrqEnable = !even;
            if (even) {
                mIrqAsserted = false;
            }
            break;
        }
    }

    std::uint8_t chrRead(std::uint16_t addr) const override
    {
        if (m_cart.chr.empty()) { // CHR-RAM
            return m_cart.chrRam.empty()
                ? 0
                : m_cart.chrRam[chrOffset(addr) % m_cart.chrRam.size()];
        }
        return m_cart.chr[chrOffset(addr) % m_cart.chr.size()];
    }

    void chrWrite(std::uint16_t addr, std::uint8_t value) override
    {
        if (m_cart.chr.empty() && !m_cart.chrRam.empty()) {
            m_cart.chrRam[chrOffset(addr) % m_cart.chrRam.size()] = value;
        }
    }

    Mirroring mirroring() const override
    {
        return (mMirroring & 1) != 0 ? Mirroring::Horizontal
                                     : Mirroring::Vertical;
    }

    // A12-rise IRQ clocking (PPU-driven; task T057 wiring).
    void clockIrq() override
    {
        // Reload from the latch when the counter is 0 (automatic) or was
        // cleared by $C001 (reload flag); otherwise decrement.
        const std::uint8_t before = mIrqCounter;
        if (mReloadPending || mIrqCounter == 0) {
            mIrqCounter = mIrqLatch;
            mReloadPending = false;
        } else {
            --mIrqCounter;
        }
        if (!mIrqEnable) {
            return;
        }
        if (mRevA) {
            // Revision A ("alternate", submapper 4): IRQ only on a CHANGE
            // to zero — a decrement reaching 0, or a $C001 reload landing
            // on 0 (the write put the counter above 127 first). An
            // automatic reload of 0 onto 0 does not re-assert.
            if (before != 0 && mIrqCounter == 0) {
                mIrqAsserted = true;
            }
        } else {
            // Revision B ("normal", submapper 0): IRQ whenever the counter
            // is 0 after reload or decrement — a zero latch asserts on
            // every clock (mmc3_irq_tests 4 and 6).
            if (mIrqCounter == 0) {
                mIrqAsserted = true;
            }
        }
    }

    bool irqLine() const override { return mIrqAsserted; }

    void setRevisionA(bool revA) override { mRevA = revA; }

    void saveState(StateWriter& w) const override
    {
        w.begin("MAP ");
        w.w<std::uint16_t>(m_cart.info.mapperId);
        w.w(mBankMode);
    w.w<std::uint8_t>(mRevA ? 1 : 0);
        w.w(mPrg[0]);
        w.w(mPrg[1]);
        for (const std::uint8_t b : mChr) {
            w.w(b);
        }
        w.w(mMirroring);
        w.w(mWramProtect);
        w.w(mIrqLatch);
        w.w(mIrqCounter);
        w.w(mIrqEnable);
        w.w(mReloadPending);
        w.w(mIrqAsserted);
        w.end();
    }

    void loadState(StateReader& r) override
    {
        (void)r.get<std::uint16_t>(); // mapperId (validated at load)
        mBankMode = r.get<std::uint8_t>();
    mRevA = r.get<std::uint8_t>() != 0;
        mPrg[0] = r.get<std::uint8_t>();
        mPrg[1] = r.get<std::uint8_t>();
        for (std::uint8_t& b : mChr) {
            b = r.get<std::uint8_t>();
        }
        mMirroring = r.get<std::uint8_t>();
        mWramProtect = r.get<std::uint8_t>();
        mIrqLatch = r.get<std::uint8_t>();
        mIrqCounter = r.get<std::uint8_t>();
        mIrqEnable = r.get<std::uint8_t>() != 0;
        mReloadPending = r.get<std::uint8_t>() != 0;
        mIrqAsserted = r.get<std::uint8_t>() != 0;
    }

private:
    // Pattern-bank offset for a PPU address under the current CHR mode.
    // CHR layout in 1K windows (nesdev MMC3, "Bank select"): with bank-select
    // bit 7 clear, R0/R1 are 2K banks at $0000/$0800 (low bit ignored) and
    // R2-R5 are 1K banks at $1000/$1400/$1800/$1C00; with bit 7 set the two
    // halves swap (R2-R5 at $0000-$0FFF, R0/R1 at $1000/$1800).
    std::size_t chrOffset(std::uint16_t addr) const
    {
        std::size_t slot = (addr >> 10) & 7;
        if ((mBankMode & 0x80) != 0) {
            slot ^= 4; // swap the 4K halves
        }
        std::size_t bank;
        switch (slot) {
        case 0: bank = mChr[0] & 0xFE; break;
        case 1: bank = mChr[0] | 0x01; break;
        case 2: bank = mChr[1] & 0xFE; break;
        case 3: bank = mChr[1] | 0x01; break;
        default: bank = mChr[slot - 2]; break; // slots 4-7 = R2-R5
        }
        return bank * 1024 + (addr & 0x3FF);
    }

    std::uint8_t mBankMode = 0;
    std::uint8_t mPrg[2] = {0, 0};
    std::uint8_t mChr[6] = {0, 0, 0, 0, 0, 0};
    std::uint8_t mMirroring = 0;
    std::uint8_t mWramProtect = 0;
    std::uint8_t mIrqLatch = 0;
    std::uint8_t mIrqCounter = 0;
    bool mIrqEnable = false;
    bool mReloadPending = false;
    bool mRevA = false; // NES 2.0 submapper 4 => revision A IRQ rule
    bool mIrqAsserted = false;
};

} // namespace

std::unique_ptr<Mapper> makeMmc3(Cartridge& cartridge)
{
    return std::make_unique<Mmc3>(cartridge);
}

} // namespace cutenes
