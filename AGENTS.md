# VST workspace

Goal: build VST3 plugins, mainly MIDI-output sequencers (event out, little/no audio). macOS arm64, Xcode 26.5, CMake 4.0, Ninja available. Workspace root is a git repo (origin `github.com/dphelmer/vst-sequencers`, branch `main`); `build/` dirs are gitignored.

## Layout
- `vst3sdk/` — Steinberg VST3 SDK, tag `v3.8.1_build_84` (MIT), cloned `--recursive --depth 1` (submodules: base, cmake, doc, pluginterfaces, public.sdk, tutorials, vstgui4). Treat as vendored: do not edit. It is a git submodule of the root repo (pinned commit, detached HEAD); fresh clones need `git clone --recursive` or `git submodule update --init --recursive`.
- Plugins live in sibling dirs (e.g. `<plugin>/`) with their own `CMakeLists.txt`. Build output goes in `build/` dirs, not inside `vst3sdk/`.
- `transport-probe/` — MIDI-out probe plug-in for testing host transport behavior (Live). Template for new plugins: copy its `CMakeLists.txt` + `source/` layout (Processor + no-param Controller + `entry.cpp`, silent stereo audio out + event in/out, `PlugType::kInstrument`). Build: `cmake -S transport-probe -B transport-probe/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSMTG_CREATE_PLUGIN_LINK=OFF && cmake --build transport-probe/build --target TransportProbe` (validator passes 47/47). Offline logic test (fake transport, no host): `--target ProbeSim` then `transport-probe/build/bin/Release/ProbeSim`. Not installed in `~/Library/Audio/Plug-Ins/VST3` unless built with link ON or copied by hand.
- Offline docs: `vst3sdk/doc/`. Online: https://steinbergmedia.github.io/vst3_dev_portal/

## Building a plugin against the SDK (verified)
Use the SDK as a subdirectory from the plugin's CMakeLists:
```cmake
cmake_minimum_required(VERSION 3.25)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "")
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "")   # ON only if using VSTGUI
add_subdirectory(<path>/vst3sdk vst3sdk)
smtg_enable_vst3_sdk()
project(MyPlugin VERSION 1.0.0)                      # VERSION is mandatory, else configure fails
smtg_add_vst3plugin(MyPlugin source/...cpp)
target_link_libraries(MyPlugin PRIVATE sdk)
smtg_target_configure_version_file(MyPlugin)         # needed if sources include "projectversion.h" (SDK example version.h does)
```
- Configure/build: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target MyPlugin`
- Output: `build/VST3/<Config>/MyPlugin.vst3`.
- The SDK `validator` is built automatically and runs against the plugin as a POST_BUILD step (`SMTG_RUN_VST_VALIDATOR`); a build "fails" if validation fails. Binary: `build/bin/<Config>/validator <path>.vst3`.
- `SMTG_CREATE_PLUGIN_LINK` defaults ON: builds symlink the `.vst3` into `~/Library/Audio/Plug-Ins/VST3` (which already holds other user plugins — don't delete them). Pass `-DSMTG_CREATE_PLUGIN_LINK=OFF` for throwaway/probe builds.
- SDK default macOS deployment target is 10.13 and `SMTG_BUILD_UNIVERSAL_BINARY` defaults ON for Xcode generator; Xcode >= 15 gets an ad-hoc `codesign` post-build step automatically.
- Configuring the SDK standalone (`cmake -S vst3sdk -B <dir>`) builds all examples + hosting tools (validator, editorhost, audiohost); only for experiments. Harmless warning: LIBJACK not found.

## MIDI-output plugin notes (from SDK headers/samples)
- Reference sample: `vst3sdk/public.sdk/samples/vst/legacymidiccout/` (emits events via `data.outputEvents->addEvent`) and `note_expression_synth/`, `again_sampleaccurate/`.
- Declare output with `addEventOutput(STR16("Event Out"), 16)` in `Processor::initialize` (`public.sdk/source/vst/vstaudioeffect.h`). Add `addEventInput` for MIDI-in-driven behavior.
- Plugin category (in factory class info) is `PlugType::kInstrument` or `kFxInstrument` (`pluginterfaces/vst/ivstaudioprocessor.h`) — host decides whether it can be placed on an instrument/MIDI track from this plus bus config; test in the target DAW.
- Sequencer timing comes from `data.processContext` (`ProcessContext` in `pluginterfaces/vst/ivstprocesscontext.h`): check `state` flags (`kPlaying`, `kTempoValid`, `kProjectTimeMusicValid`, `kTimeSigValid`) before reading `tempo`/`projectTimeMusic`; they are optional. Event `sampleOffset` is relative to the block start and must be < `numSamples`.
- Note on/off: `Event::kNoteOnEvent` / `kNoteOffEvent` need matching `noteId`; send note-offs on transport stop / deactivate to avoid stuck notes. Raw CC/pitch-bend/etc. out requires `kLegacyMIDICCOutEvent` (`Helpers::initLegacyMIDICCOutEvent`), host support varies.
- `process()` is realtime: no allocation, locks, or I/O.
- Processor and controller are separate classes with distinct CIDs (`plugcids.h` pattern); never reuse the SDK sample FUIDs for real plugins — generate new ones.
- Trademark: don't put "VST" in the plugin/company name.

## Transport sync / Ableton Live (target host)
Sources: SDK headers (verified) + Ableton help + JUCE forum reports (community, not tested here). Anything marked UNTESTED needs a probe plugin in Live.
- **Request transport fields or get none.** `ProcessContext` fields are only filled if the plugin asks via `IProcessContextRequirements` (since SDK 3.7, header says you "may not get any information at all" otherwise). `AudioEffect` implements it but defaults to flags 0 and the validator still passes. In the Processor constructor: `processContextRequirements.needTransportState().needProjectTimeMusic().needTempo().needTimeSignature();` (add `.needBarPositionMusic()`, `.needCycleMusic()` if used). Flags are read once between `initialize` and `setActive`; can't change later.
- Still check the `state` valid flags every block; `projectTimeSamples` and `sampleRate` are the only always-valid fields.
- **Live VST3 has no "MIDI effect" slot.** VST3 has no MIDI-effect category and Live won't put third-party MIDI plugins before an instrument on one track. Pattern: sequencer plugin on its own MIDI track, second MIDI track with `MIDI From` = sequencer track + plugin channel, monitor `In`, instrument on that second track (Ableton help "Accessing the MIDI output of a VST plug-in"). Live merges all MIDI channels when routing track-to-track, so don't rely on channel to split lanes.
- **Must declare an audio output bus** (e.g. stereo, silent) and be an instrument category, or Live refuses to load a pure-MIDI VST3 (JUCE forum; JUCE's own MidiLogger special-cases Live for this). Keep `addEventInput` too if it should react to incoming notes.
- **Only note on/off are known to work out of Live.** `kLegacyMIDICCOutEvent` (CC/pitch bend) reportedly has no effect in Live VST3 (JUCE forum threads 2020-2022; UNTESTED on Live 12). Prefer notes for sequencing; test CC with a probe before designing around it. AU can't do MIDI out at all (Ableton help), so VST3 is the right format.
- **Derive position from `projectTimeMusic` (ppq) at block start, not from `projectTimeSamples`.** Reported in Live: sample time is non-linear when tempo changes; ppq stays continuous. Tempo changes mid-block are not visible until the next block, so compute step boundaries per block as `ppq + (sampleOffset/sampleRate) * tempo/60` and accept small drift on tempo ramps.
- Detect jumps (loop wrap, locate, clip relaunch) by comparing this block's ppq to the previous block's expected ppq (`prev + numSamples/sampleRate*tempo/60`); on a jump, send note-offs for held notes and re-seat the step index. Host may not split blocks at loop points (documented as optional). Don't assume `kCycleActive` is set in Live (UNTESTED).
- Transport stop: when `kPlaying` goes 1->0 flush note-offs for notes this plugin started (track them; don't blast 128x16 note-offs). Also flush in `setActive(false)` / `setProcessing(false)`. Hosts may keep calling `process()` while stopped (UNTESTED in Live), so gate generation on `kPlaying`.
- Session View clip launch is quantized by Live's launch quantization, but the plugin only sees the global transport/ppq; a sequencer plugin on a MIDI track is not clip-aware. Don't expect clip-relative position. UNTESTED how ppq behaves when only Session clips play with Arrangement stopped.
- Live compensates plugin latency (PDC). A MIDI-only plugin should report 0 latency; MIDI from the plugin reaches the routed track through the host. Whether Live adds a block of delay on track-to-track MIDI is UNTESTED; measure by recording the routed MIDI into a clip.
- Live (at least 11) has been reported to not set `processMode` to `kOffline` during export; don't rely on it to detect bounce.
- SDK `host-checker` sample (`public.sdk/samples/vst/hostchecker`, needs `SMTG_ENABLE_VSTGUI_SUPPORT=ON`) reports which `ProcessContext` fields a host actually provides. It's an audio FX with no event I/O; build with `-DSMTG_CREATE_PLUGIN_LINK=OFF` and copy manually if wanted. A purpose-built MIDI-out "transport probe" plugin is a better first test in Live.

### Transport probe: how to run it in Live
- Plug-in emits note on at every quarter-note boundary (pitch 72 on bar downbeat, else 60; off 0.25 beat later) plus legacy CC1 on the same beats, only while `kPlaying`. It logs to `~/Library/Logs/TransportProbe/` (`blocks.csv` per process block, `events.csv` incoming events, `lifecycle.log`); files are truncated on each `setActive(true)`, so one instance at a time. Summarize with `python3 transport-probe/tools/analyze.py`.
- Test matrix (answers the UNTESTED items above): stop/start; Arrangement loop; locate while playing; tempo ramp/change; Session clip launch with Arrangement stopped; export/bounce; read analyze.py output for `kCycleActive`, calls-while-stopped, jumps, processMode. For latency/CC: route to a second MIDI track (see pattern above), record into a clip, check note position vs grid and whether CC1 arrives.
- Probe logic is checked offline by `ProbeSim` only; real Live behavior is still unverified until someone runs it.
