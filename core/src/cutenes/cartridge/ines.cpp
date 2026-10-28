// iNES 1.0 and NES 2.0 cartridge parsing (task T018).
//
// Format reference: nesdev wiki iNES page; constants per research.md.
// Every rejection names the exact defect (spec FR-001).

#include "cutenes/cartridge/cart.hpp"

#include "cutenes/mappers/mapper.hpp"

#include <cstring>

namespace cutenes {

namespace {

constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kTrainerSize = 512;
constexpr std::size_t kPrgBank = 16384;
constexpr std::size_t kChrBank = 8192;

bool isNes2Header(const std::uint8_t* h)
{
    // Byte 7 bits 3-4 == 0b10 identifies NES 2.0.
    return (h[7] & 0x18) == 0x08;
}

} // namespace

LoadResult Cartridge::load(const std::uint8_t* data, std::size_t size,
    Cartridge& out)
{
    out = Cartridge{}; // never leave partial state on rejection

    if (size < kHeaderSize) {
        return {LoadError::NotAnInesFile, 0, {}};
    }
    if (std::memcmp(data, "NES\x1A", 4) != 0) {
        return {LoadError::NotAnInesFile, 0, {}};
    }

    const std::uint8_t* h = data;
    const bool nes2 = isNes2Header(h);
    if ((h[7] & 0x0C) == 0x0C) {
        // Bits 3-4 values 00/10 are legal (iNES 1.0 / NES 2.0); 11 is
        // reserved and 01 with bit2 set is not a valid combination.
        return {LoadError::Nes2FieldInvalid, 7, {}};
    }

    CartridgeInfo info;
    info.formatVersion = nes2 ? 2 : 0;

    // PRG/CHR sizes. NES 2.0 extends both to 24 bits across bytes 4/5/9.
    std::uint32_t prgBanks = h[4];
    std::uint32_t chrBanks = h[5];
    std::size_t chrRamSize = 0; // NES 2.0 byte 11 (0 => default 8K)
    if (nes2) {
        prgBanks |= static_cast<std::uint32_t>((h[9] & 0x0F) << 8);
        chrBanks |= static_cast<std::uint32_t>((h[9] & 0xF0) << 4);
    }
    if (prgBanks == 0) {
        return {LoadError::InvalidBankCounts, 4, {}};
    }

    info.prgSize = static_cast<std::size_t>(prgBanks) * kPrgBank;
    info.chrSize = static_cast<std::size_t>(chrBanks) * kChrBank;

    // Flags 6.
    info.mirroring = (h[6] & 0x08) != 0
        ? Mirroring::FourScreen
        : ((h[6] & 0x01) != 0 ? Mirroring::Vertical : Mirroring::Horizontal);
    info.battery = (h[6] & 0x02) != 0;
    const bool trainer = (h[6] & 0x04) != 0;

    // Mapper id: iNES 1.0 keeps the ambiguous middle nibble; NES 2.0 defines
    // it fully (byte 7 high nibble + byte 8 low nibble).
    std::uint16_t mapperId = static_cast<std::uint16_t>(
        (h[6] >> 4) | ((h[7] & 0xF0)));
    if (nes2) {
        mapperId = static_cast<std::uint16_t>(mapperId | (h[8] & 0x0F) << 8);
        // Byte 10: low nibble = volatile PRG-RAM, high nibble = battery
        // (NVRAM) PRG-RAM, each 64 << shift bytes; a shift of 0 means none.
        const auto ramBytes = [](std::uint8_t shift) -> std::size_t {
            return shift != 0 && shift < 15 ? std::size_t{64} << shift : 0;
        };
        const std::size_t nvram = ramBytes(h[10] >> 4);
        info.prgRamSize = ramBytes(h[10] & 0x0F) + nvram;
        if (nvram != 0) {
            info.battery = true; // NVRAM implies battery backing
        }
        // Byte 11: CHR-RAM size shift (64 << n; 0x9 => 32K); byte 12
        // bits 0-1 are the TV system (0 NTSC, 1 PAL).
        const std::uint8_t chrShift = h[11] & 0x0F;
        if (chrShift != 0 && chrShift < 15) {
            chrRamSize = std::size_t{64} << chrShift;
        }
        switch (h[12] & 0x03) {
        case 0: info.region = Region::Ntsc; break;
        case 1: info.region = Region::Pal; break;
        default: info.region = Region::Unspecified; break;
        }
    } else {
        // iNES 1.0 byte 8 low nibble: PRG-RAM in 8K units, 0 infers 8K for
        // compatibility (research.md D10).
        const std::uint32_t ramBanks = h[8] & 0x0F;
        info.prgRamSize = static_cast<std::size_t>(
            ramBanks == 0 ? 1 : ramBanks) * 8192;
        // Byte 9 bit 0 is the (unreliable) iNES 1.0 TV flag; treat as a
        // hint only — Unspecified lets the machine default to NTSC.
        info.region = (h[9] & 0x01) != 0 ? Region::Unspecified
                                         : Region::Unspecified;
    }
    info.mapperId = mapperId;
    // NES 2.0 byte 13 low nibble: submapper (e.g. mapper 4 submapper 4 =
    // MMC3 revision A IRQ behavior; nesdev wiki MMC3, IRQ Specifics).
    info.submapper = nes2 ? static_cast<std::uint8_t>(h[13] & 0x0F) : 0;

    // Payload bounds.
    const std::size_t trainerSize = trainer ? kTrainerSize : 0;
    const std::size_t payload = info.prgSize + info.chrSize;
    if (size < kHeaderSize + trainerSize + payload) {
        return {LoadError::TruncatedFile, 0, {}};
    }

    // Copy the cartridge data.
    const std::uint8_t* body = data + kHeaderSize + trainerSize;
    out.prg.assign(body, body + info.prgSize);
    if (info.chrSize > 0) {
        out.chr.assign(body + info.prgSize, body + info.prgSize + info.chrSize);
    } else {
        out.chrRam.assign(chrRamSize != 0 ? chrRamSize : kChrBank, 0);
    }
    out.prgRam.assign(info.prgRamSize, 0);

    // Trainer (if any) loads into the tail of PRG-RAM per convention.
    if (trainer && !out.prgRam.empty()) {
        const std::size_t dst = out.prgRam.size() >= kTrainerSize
            ? out.prgRam.size() - kTrainerSize
            : 0;
        std::memcpy(out.prgRam.data() + dst, data + kHeaderSize,
            out.prgRam.size() >= kTrainerSize ? kTrainerSize
                                              : out.prgRam.size());
    }

    out.info = info;

    auto mapperInstance = createMapper(mapperId, out);
    if (mapperInstance == nullptr) {
        return {LoadError::UnsupportedMapper, mapperId, info};
    }
    out.mapper = std::move(mapperInstance);
    return {LoadError::Ok, 0, info};
}

} // namespace cutenes
