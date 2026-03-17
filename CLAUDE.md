# 🔧 bambu-fw-fetch — Firmware Fetch Tool

> forge ❯ bambu ❯ **bfw** · application
>
> **Archetype:** Native CLI tool + Python wrapper
> **Language:** C++17 (clang++) + Python 3
> **Interfaces:** CLI · MQTT (TLS port 8883)
> **Shared rules:** `~/ai/forge/bambu/.claude/rules/bambu-ecosystem.md`

## Build / Validate

- Build: `make`
- macOS only (ARM64), requires BambuStudio installed

## Architecture

- C++17 clang++ binary (147 lines), loads `libbambu_networking.dylib` from BambuStudio
- Two modes: `./bambu_fw_fetch <DEV_ID>` (firmware manifest) or `--info` (cloud print history)
- Python wrapper: MQTT `upgrade_confirm` over TLS port 8883 (requires typed "YES")
- `destroy_agent` call is mandatory before process exit
- BIMH firmware signing key has NOT been leaked (the Jan 2025 leak was the MQTT auth key)

## Git Policy

Agent-managed. Full git lifecycle authorized: stage, commit, push.
