// MMC3 (mapper 4) — task T056.
//
// PRG: 8K windows — $8000 = R6 or R7 by mode bit 6, $A000 = R7, $C000 =
// the other of R6/R7. CHR: 2Kx4 or 1Kx8 by mode bit 7. A12-rise IRQ
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
        std::size_t bank;
        if (addr < 0xA000) {
            bank = (mBankMode & 0x40) == 0 ? bankOf(0) : bankOf(1);
        } else if (addr < 0xC000) {
            bank = bankOf(1);
        } else {
            bank = (mBankMode & 0x40) == 0 ? bankOf(1) : bankOf(0);
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
                const std::uint8_t reg = static_cast<std::uint8_t>(
                    value & 7);
                if (reg < 6) {
                    mChr[reg] = value;
                } else {
                    mPrg[reg - 6] = static_cast<std::uint8_t>(
                        value & 0x0F);
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
                mReloadPending = true;
            }
            break;
        default: // $E000/$E001
            mIrqEnable = !even;
            if (even) {
                mIrqAsserted = false; // disable acknowledges
            }
            break;
        }
    }

    std::uint8_t chrRead(std::uint16_t addr) const override
    {
        if (m_cart.chr.empty()) { // CHR-RAM
            return m_cart.chrRam.empty()
                ? 0
                : m_cart.chrRam[addr & (m_cart.chrRam.size() - 1)];
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
        if (mReloadPending) {
            mIrqCounter = mIrqLatch;
            mReloadPending = false;
        } else if (mIrqCounter == 0) {
            mIrqCounter = mIrqLatch;
        } else {
            --mIrqCounter;
            if (mIrqCounter == 0 && mIrqEnable) {
                mIrqAsserted = true;
            }
        }
    }

    bool irqLine() const override { return mIrqAsserted; }

    void saveState(StateWriter& w) const override
    {
        w.begin("MAP ");
        w.w<std::uint16_t>(m_cart.info.mapperId);
        w.w(mBankMode);
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
    std::size_t chrOffset(std::uint16_t addr) const
    {
        if ((mBankMode & 0x80) == 0) {
            // 2K mode: R0 -> $0000-$0FFF, R1 -> $1000-$1FFF (register bit
            // 7 selects the inner 1K pair).
            const std::uint8_t reg = addr < 0x1000 ? mChr[0] : mChr[1];
            const std::size_t pair = static_cast<std::size_t>(reg & 0x7E);
            const std::size_t inner = (addr >> 10) & 1;
            return (pair * 2 + ((reg >> 7) & 1) * 2 + inner) * 1024
                + (addr & 0x3FF);
        }
        // 1K mode: R2/R3 -> $0000/$0400, R4/R5 -> $1000/$1400.
        const std::size_t idx = static_cast<std::size_t>(
            (addr < 0x1000 ? 2 : 4) + ((addr >> 10) & 1));
        return static_cast<std::size_t>(mChr[idx]) * 1024
            + (addr & 0x3FF);
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
    bool mIrqAsserted = false;
};

} // namespace

std::unique_ptr<Mapper> makeMmc3(Cartridge& cartridge)
{
    return std::make_unique<Mmc3>(cartridge);
}

} // namespace cutenes
