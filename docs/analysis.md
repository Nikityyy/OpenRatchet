# OpenRatchet Analysis Report

This document contains notes and findings from analyzing the original PlayStation 2 Ratchet & Clank (2002) ELF executable.

## Memory Map
- **EE RAM**: 32MB standard PS2 allocation
- **GS Registers**: Mapped at `0x12000000`
- **Syscall Table**: Custom guest syscall table mapped in high memory

## Known Functions (Manual Overrides)
During static recompilation, certain functions cannot be fully traced or are called indirectly via pointers. These are manually specified to ensure the recompiler generates their bodies:
- `0x0011DC18` (CRT0 / startup)
- `0x001E9488`
- `0x001E9658`

*Note: As more indirect jumps are found at runtime (resulting in `MISSING-TARGET` errors), they will be added to the manual override list in `tools/analyze.py`.*

## System Calls
The game relies heavily on standard PS2 BIOS syscalls:
- **Threads**: `CreateThread` (0x20), `StartThread` (0x22), `SleepThread` (0x2B), etc.
- **Sync**: `CreateSema` (0x40), `WaitSema` (0x44), `SignalSema` (0x42)
- **IOP/RPC**: `sceSifBindRpc`, `sceSifCallRpc` are used to communicate with the IOP for CDVD reads and PAD input.
- **CDVD Server ID**: `0x80000059` (Standard SCE CDVD module)

## Rendering Architecture
The game builds GIF packets in EE RAM and uses DMA channel 2 (VIF1/GIF) to send them to the Graphics Synthesizer. The custom GS renderer intercepts these GIF packets to reconstruct drawing state.
