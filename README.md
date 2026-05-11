# projectMM v2

A cross-platform runtime for modular, loop-driven processes. Small units of work (`Module`s) with a `setup()` / `loop()` / `loop20ms()` / `loop1s()` / `loop10s()` / `teardown()` lifecycle, scheduled across multiple cores as an arbitrary DAG of stages connected by lock-free SPSC ring buffers. Targets: ESP32 (classic, S3, P4), Raspberry Pi, and PC (Linux, macOS, Windows) as peer builds from commit 1.

The entry domain is light control: LEDs and other light sources driven directly via GPIO or over a network (Art-Net, DDP, E1.31). The lighting domain lives in `modules/lights/`; it depends on the core, not the other way around. The same runtime is shaped to fit audio processing, sensor pipelines, and other repeating-process domains without modification.

## Status

Pre-release. v2 is being brought to parity with [v1](https://github.com/ewowi/projectMM) over six sprints — see [Release 1 — Restart to Parity](docs/development/release-01.md). The main driver is **frugality**: frugal in code, in CPU cycles, and in resources. Guardrails landed in Sprint 1 enforce that property mechanically; the anti-drift rules in [docs/development/anti-drift.md](docs/development/anti-drift.md) keep it from eroding once AI agents do most of the writing.

## Docs

[https://ewowi.github.io/projectMM-v2](https://ewowi.github.io/projectMM-v2)

## Lineage

v2 is the successor to [projectMM](https://github.com/ewowi/projectMM) (v1.0.0 through v1.8.0). The decision to restart is documented in v1's [Release 9](https://ewowi.github.io/projectMM/development/release-09/). v1 is frozen at `v1.8.0-pre-restart`; this repo is where active development happens.

## License

GPL-3.0.
