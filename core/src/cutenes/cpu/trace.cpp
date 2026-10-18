// CPU trace/disassembly matching nestest.log's exact line format (task
// T029). Reference line shape (Nintendulator golden log):
//   "C000  4C F5 C5  JMP $C5F5                       A:00 X:00 Y:00 P:24 SP:FD PPU:  0, 21 CYC:7"
// Memory-operand instructions append " = XX" with the pre-execution value.

#include "cutenes/cpu/trace.hpp"

#include "cutenes/cpu/cpu.hpp"

#include <cstdio>
#include <string>

namespace cutenes {

namespace {

const char* const kMnemonic[256] = {
    "BRK", "ORA", "KIL", "SLO", "NOP", "ORA", "ASL", "SLO",
    "PHP", "ORA", "ASL", "ANC", "NOP", "ORA", "ASL", "SLO",
    "BPL", "ORA", "KIL", "SLO", "NOP", "ORA", "ASL", "SLO",
    "CLC", "ORA", "NOP", "SLO", "NOP", "ORA", "ASL", "SLO",
    "JSR", "AND", "KIL", "RLA", "BIT", "AND", "ROL", "RLA",
    "PLP", "AND", "ROL", "ANC", "BIT", "AND", "ROL", "RLA",
    "BMI", "AND", "KIL", "RLA", "NOP", "AND", "ROL", "RLA",
    "SEC", "AND", "NOP", "RLA", "NOP", "AND", "ROL", "RLA",
    "RTI", "EOR", "KIL", "SRE", "NOP", "EOR", "LSR", "SRE",
    "PHA", "EOR", "LSR", "ALR", "JMP", "EOR", "LSR", "SRE",
    "BVC", "EOR", "KIL", "SRE", "NOP", "EOR", "LSR", "SRE",
    "CLI", "EOR", "NOP", "SRE", "NOP", "EOR", "LSR", "SRE",
    "RTS", "ADC", "KIL", "RRA", "NOP", "ADC", "ROR", "RRA",
    "PLA", "ADC", "ROR", "ARR", "JMP", "ADC", "ROR", "RRA",
    "BVS", "ADC", "KIL", "RRA", "NOP", "ADC", "ROR", "RRA",
    "SEI", "ADC", "NOP", "RRA", "NOP", "ADC", "ROR", "RRA",
    "NOP", "STA", "NOP", "SAX", "STY", "STA", "STX", "SAX",
    "DEY", "NOP", "TXA", "XAA", "STY", "STA", "STX", "SAX",
    "BCC", "STA", "KIL", "SHA", "STY", "STA", "STX", "SAX",
    "TYA", "STA", "TXS", "SHS", "SHY", "STA", "SHX", "SHA",
    "LDY", "LDA", "LDX", "LAX", "LDY", "LDA", "LDX", "LAX",
    "TAY", "LDA", "TAX", "LAX", "LDY", "LDA", "LDX", "LAX",
    "BCS", "LDA", "KIL", "LAX", "LDY", "LDA", "LDX", "LAX",
    "CLV", "LDA", "TSX", "LAS", "LDY", "LDA", "LDX", "LAX",
    "CPY", "CMP", "NOP", "DCP", "CPY", "CMP", "DEC", "DCP",
    "INY", "CMP", "DEX", "SBX", "CPY", "CMP", "DEC", "DCP",
    "BNE", "CMP", "KIL", "DCP", "NOP", "CMP", "DEC", "DCP",
    "CLD", "CMP", "NOP", "DCP", "NOP", "CMP", "DEC", "DCP",
    "CPX", "SBC", "NOP", "ISB", "CPX", "SBC", "INC", "ISB",
    "INX", "SBC", "NOP", "SBC", "CPX", "SBC", "INC", "ISB",
    "BEQ", "SBC", "KIL", "ISB", "NOP", "SBC", "INC", "ISB",
    "SED", "SBC", "NOP", "ISB", "NOP", "SBC", "INC", "ISB",
};

int operandLength(Cpu::Mode mode)
{
    switch (mode) {
    case Cpu::Mode::Imm:
    case Cpu::Mode::Zpg:
    case Cpu::Mode::Zpx:
    case Cpu::Mode::Zpy:
    case Cpu::Mode::Izx:
    case Cpu::Mode::Izy:
    case Cpu::Mode::Rel:
        return 1;
    case Cpu::Mode::Abs:
    case Cpu::Mode::Abx:
    case Cpu::Mode::Aby:
    case Cpu::Mode::Ind:
        return 2;
    default:
        return 0;
    }
}


// Unofficial opcodes display with a leading '*' (Nintendulator style).
bool isUnofficial(std::uint8_t op)
{
    switch (op) {
    case 0x03: case 0x04: case 0x07: case 0x0B: case 0x0C: case 0x0F:
    case 0x13: case 0x14: case 0x17:
    case 0x1A: case 0x1B: case 0x1C: case 0x1F: case 0x23: case 0x27: case 0x2B:
    case 0x2F: case 0x33: case 0x34: case 0x37: case 0x3A: case 0x3B: case 0x3C:
    case 0x3F: case 0x43: case 0x44: case 0x47: case 0x4B: case 0x4F:
    case 0x53:
    case 0x54: case 0x57: case 0x5A: case 0x5B: case 0x5C: case 0x5F:
    case 0x63: case 0x64: case 0x67: case 0x6B: case 0x6F: case 0x73:
    case 0x74: case 0x77:
    case 0x7A: case 0x7B: case 0x7C: case 0x7F: case 0x80: case 0x82:
    case 0x83: case 0x87: case 0x89: case 0x8B: case 0x8F: case 0x93:
    case 0x97: case 0x9B: case 0x9C: case 0x9E: case 0x9F: case 0xA3:
    case 0xA7: case 0xAB: case 0xAF: case 0xB3: case 0xB7: case 0xBB:
    case 0xBF: case 0xC2: case 0xC3: case 0xC7: case 0xCB: case 0xCF:
    case 0xD3: case 0xD4: case 0xD7: case 0xDA: case 0xDB: case 0xDC: case 0xDF:
    case 0xE2: case 0xE3: case 0xE7: case 0xEB: case 0xEF: case 0xF3:
    case 0xF4: case 0xF7: case 0xFA: case 0xFB: case 0xFC: case 0xFF:
    // KIL/JAM opcodes
    case 0x02: case 0x12: case 0x22: case 0x32: case 0x42: case 0x52:
    case 0x62: case 0x72: case 0x92: case 0xB2: case 0xD2: case 0xF2:
        return true;
    default:
        return false;
    }
}

const char* hex2(std::uint8_t v)
{
    static char buf[2][3];
    static int slot = 0;
    slot = (slot + 1) % 2;
    std::snprintf(buf[slot], sizeof buf[slot], "%02X", v);
    return buf[slot];
}

const char* hex4(std::uint16_t v)
{
    static char buf[2][5];
    static int slot = 0;
    slot = (slot + 1) % 2;
    std::snprintf(buf[slot], sizeof buf[slot], "%04X", v);
    return buf[slot];
}

} // namespace

std::string traceLine(const TracePoint& tp, Peek peek)
{
    const std::uint8_t op = peek(tp.cpu.pc);
    const auto& info = Cpu::opcodeInfo(op);
    const int operands = operandLength(info.mode);
    const std::uint8_t lo = operands >= 1 ? peek(tp.cpu.pc + 1) : 0;
    const std::uint16_t hiAddr = static_cast<std::uint16_t>(tp.cpu.pc + 2);
    const std::uint8_t hi = operands >= 2 ? peek(hiAddr) : 0;

    char bytes[16];
    if (operands == 0) {
        std::snprintf(bytes, sizeof bytes, "%02X", op);
    } else if (operands == 1) {
        std::snprintf(bytes, sizeof bytes, "%02X %02X", op, lo);
    } else {
        std::snprintf(bytes, sizeof bytes, "%02X %02X %02X", op, lo, hi);
    }

    char dis[32];
    char name[6];
    std::snprintf(name, sizeof name, "%s%s",
        isUnofficial(op) ? "*" : "", kMnemonic[op]);
    const char* m = name;
    const std::uint16_t base16 =
        static_cast<std::uint16_t>(lo | (hi << 8));
    switch (info.mode) {
    case Cpu::Mode::Acc: std::snprintf(dis, sizeof dis, "%s A", m); break;
    case Cpu::Mode::Imm: std::snprintf(dis, sizeof dis, "%s #$%02X", m, lo); break;
    case Cpu::Mode::Zpg: std::snprintf(dis, sizeof dis, "%s $%s", m, hex2(lo)); break;
    case Cpu::Mode::Zpx: std::snprintf(dis, sizeof dis, "%s $%s,X", m, hex2(lo)); break;
    case Cpu::Mode::Zpy: std::snprintf(dis, sizeof dis, "%s $%s,Y", m, hex2(lo)); break;
    case Cpu::Mode::Abs: std::snprintf(dis, sizeof dis, "%s $%s", m, hex4(base16)); break;
    case Cpu::Mode::Abx: std::snprintf(dis, sizeof dis, "%s $%s,X", m, hex4(base16)); break;
    case Cpu::Mode::Aby: std::snprintf(dis, sizeof dis, "%s $%s,Y", m, hex4(base16)); break;
    case Cpu::Mode::Ind: std::snprintf(dis, sizeof dis, "%s ($%s)", m, hex4(base16)); break;
    case Cpu::Mode::Izx: std::snprintf(dis, sizeof dis, "%s ($%s,X)", m, hex2(lo)); break;
    case Cpu::Mode::Izy: std::snprintf(dis, sizeof dis, "%s ($%s),Y", m, hex2(lo)); break;
    case Cpu::Mode::Rel: {
        const std::uint16_t target = static_cast<std::uint16_t>(
            tp.cpu.pc + 2 + static_cast<std::int8_t>(lo));
        std::snprintf(dis, sizeof dis, "%s $%s", m, hex4(target));
        break;
    }
    default: std::snprintf(dis, sizeof dis, "%s", m); break; // Imp
    }

    // Memory-operand instructions (loads/stores/RMW/compares) append
    // " = XX"; program-flow ops (JMP/JSR) never do.
    char disValue[48];
    const bool dataAccess = info.mode == Cpu::Mode::Zpg
        || info.mode == Cpu::Mode::Zpx || info.mode == Cpu::Mode::Zpy
        || info.mode == Cpu::Mode::Abs || info.mode == Cpu::Mode::Abx
        || info.mode == Cpu::Mode::Aby || info.mode == Cpu::Mode::Izx
        || info.mode == Cpu::Mode::Izy;
    if (dataAccess && op != 0x4C && op != 0x20) {
        auto zpx = [&]() {
            return static_cast<std::uint16_t>((lo + tp.cpu.x) & 0xFF);
        };
        auto zpy = [&]() {
            return static_cast<std::uint16_t>((lo + tp.cpu.y) & 0xFF);
        };
        auto izxPtr = [&]() {
            return static_cast<std::uint16_t>((lo + tp.cpu.x) & 0xFF);
        };
        auto izxBase = [&]() {
            const std::uint16_t p = izxPtr();
            return static_cast<std::uint16_t>(peek(p)
                | (peek(static_cast<std::uint16_t>((p + 1) & 0xFF)) << 8));
        };
        auto izyBase = [&]() {
            return static_cast<std::uint16_t>(peek(lo)
                | (peek(static_cast<std::uint16_t>((lo + 1) & 0xFF)) << 8));
        };
        std::uint16_t ea = 0; // final data address (value fetch)
        switch (info.mode) {
        case Cpu::Mode::Zpg: ea = lo; break;
        case Cpu::Mode::Zpx: ea = zpx(); break;
        case Cpu::Mode::Zpy: ea = zpy(); break;
        case Cpu::Mode::Abs: ea = base16; break;
        case Cpu::Mode::Abx: ea = static_cast<std::uint16_t>(base16 + tp.cpu.x); break;
        case Cpu::Mode::Aby: ea = static_cast<std::uint16_t>(base16 + tp.cpu.y); break;
        case Cpu::Mode::Izx: ea = izxBase(); break;
        case Cpu::Mode::Izy:
            ea = static_cast<std::uint16_t>(izyBase() + tp.cpu.y);
            break;
        default: break;
        }
        // Golden formats (nestest.log):
        //   zpg/abs:        "LDA $07FF = 00"
        //   indexed:        "LDA $0300,Y @ 0300 = 89" / "LDY $33,X @ 33 = AA"
        //   (zp,X):         "LDA ($80,X) @ 80 = 0200 = 5A"
        //   (zp),Y:         "LDA ($89),Y = 0300 @ 0300 = 89"
        switch (info.mode) {
        case Cpu::Mode::Zpx:
            std::snprintf(disValue, sizeof disValue, "%s @ %02X = %02X",
                dis, ea, peek(ea));
            break;
        case Cpu::Mode::Zpy:
            std::snprintf(disValue, sizeof disValue, "%s @ %02X = %02X",
                dis, ea, peek(ea));
            break;
        case Cpu::Mode::Abx:
        case Cpu::Mode::Aby:
            std::snprintf(disValue, sizeof disValue, "%s @ %04X = %02X",
                dis, ea, peek(ea));
            break;
        case Cpu::Mode::Izx:
            std::snprintf(disValue, sizeof disValue, "%s @ %02X = %04X = %02X",
                dis, izxPtr(), ea, peek(ea));
            break;
        case Cpu::Mode::Izy:
            std::snprintf(disValue, sizeof disValue, "%s = %04X @ %04X = %02X",
                dis, izyBase(), ea, peek(ea));
            break;
        default:
            std::snprintf(disValue, sizeof disValue, "%s = %02X",
                dis, peek(ea));
            break;
        }
    } else if (op == 0x6C) {
        // JMP ($nnnn) shows its (page-wrapped) target.
        const std::uint16_t lo2 = peek(base16);
        const std::uint16_t hi2 = peek(static_cast<std::uint16_t>(
            (base16 & 0xFF00) | ((base16 + 1) & 0xFF)));
        std::snprintf(disValue, sizeof disValue, "%s = %04X", dis,
            static_cast<std::uint16_t>(lo2 | (hi2 << 8)));
    } else {
        std::snprintf(disValue, sizeof disValue, "%s", dis);
    }

    // Layout (golden): bytes field cols 6-15; the unofficial '*' occupies
    // col 15 with the mnemonic always starting at col 16; A: at col 48.
    char line[160];
    if (isUnofficial(op)) {
        std::snprintf(line, sizeof line,
            "%04X  %-9s%-32s A:%02X X:%02X Y:%02X P:%02X SP:%02X "
            "PPU:%3u,%3u CYC:%llu",
            tp.cpu.pc, bytes, disValue, tp.cpu.a, tp.cpu.x, tp.cpu.y,
            tp.cpu.p, tp.cpu.s, tp.scanline, tp.dot,
            static_cast<unsigned long long>(tp.cpu.masterCycle / 12));
    } else {
        std::snprintf(line, sizeof line,
            "%04X  %-10s%-31s A:%02X X:%02X Y:%02X P:%02X SP:%02X "
            "PPU:%3u,%3u CYC:%llu",
            tp.cpu.pc, bytes, disValue, tp.cpu.a, tp.cpu.x, tp.cpu.y,
            tp.cpu.p, tp.cpu.s, tp.scanline, tp.dot,
            static_cast<unsigned long long>(tp.cpu.masterCycle / 12));
    }
    return line;
}

} // namespace cutenes
