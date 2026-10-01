# ARA FL Bridge v0.2 (thought experiment, now data-driven)

Two halves joined by a sidecar JSON:

1. **FLP side** (`Source/FLPSidecar`, `Tools/flp2sidecar.cpp`): uses your `flp.h` parser to turn the playlist into audio
   regions (file, track, tick position/length) + tempo + a raw `probe` dump for reverse-engineering.
2. **Plug-in side** (`Source/*`): a VST3 effect for FL Studio that hosts an inner ARA plug-in and acts as its ARA host.
   It can be fed by (a) the sidecar (real clips, no latency), (b) a WAV import, or (c) live capture.

Build: `cmake -B build -DARA_SDK_DIR=<ARA_SDK with submodules> -DFLP_SOURCE_DIR=<folder with flp.h/flp.cpp>`
Use:   `flp2sidecar MyProject.flp` -> in FL insert "ARA FL Bridge" -> Load inner ARA VST3 -> Load FLP sidecar -> pick track.

See docs/KNOWLEDGE.md (what is assumed vs verified) and docs/INTEGRATION_FLPTOOL.md (add the export button to your tool).
