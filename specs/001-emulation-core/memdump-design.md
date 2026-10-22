# Memory Dump Functionality for Debugging

**Feature**: 001-emulation-core  
**Purpose**: Provide memory dump capability for debugging emulator state

## Requirements

- Add memdump function to core (Machine API)
- Output a `<datetime>.dmp` file in a `debug/` directory
- Support dumping relevant memory regions (CPU RAM, PPU memory, cartridge RAM, etc.)
- Make it accessible for debugging purposes
- Update documentation, plans, and specs

## API Design

```cpp
// In Machine class
void dumpMemory(const std::string& debugDir = "debug"); // creates debugDir if needed, writes <datetime>.dmp
```

Or alternatively:
```cpp
std::string dumpMemoryToFile(const std::string& debugDir = "debug");
```

## File Format
Binary dump with header containing:
- Timestamp
- Machine state info
- Memory regions dumped
- Raw memory contents

## Implementation Details
- Create debug directory if it doesn't exist
- Generate filename with ISO 8601 datetime or timestamp
- Dump CPU memory ($0000-$FFFF), PPU RAM, OAM, palettes, cartridge PRG/CHR RAM
- Make available via public API in Machine
- Update contracts/core-api.md
