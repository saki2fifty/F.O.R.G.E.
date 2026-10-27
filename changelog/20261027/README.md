# 2026-10-27 — Runtime timing, input, profiling and audio

## Audio source codecs

MP3 and FLAC now share WAV's supervised import, processing settings, selected PCM,
preview/waveform, source watcher, runtime/export and existing MCP asset workflows.
Distinct new recipes preserve WAV settings, identity and old cooked revisions.
The existing pinned miniaudio decoders are enabled; no new codec/encoder dependency
or public SDK ABI is introduced. Original mono/stereo recordings and Gallery83
compare the same WAV, FLAC and MP3 audio. Native Windows and physical listening,
external-client and new authentic screenshot acceptance remain pending.

Local source; Windows verification and numbered delivery remain pending.

- Runtime clock status exposes elapsed unscaled seconds, the latest distinct
  interval and a sample sequence through existing C++ Game queries and Play/MCP
  timing. Pause and zero/slow simulation speed retain real-time observations.
- Fixed physics/Flecs delta, catch-up bounds and command boundaries remain
  unchanged. Equal samples do not erase intervals; Step creates no wall time.
- The public time_scale_gameplay SDK example includes a per-entity control timer.
  Its opted-in RateProbe demonstration schema is version2; existing authored
  version1 examples require ordinary explicit schema migration after inspection.
- Scene switches retain the host timeline; a new Play/process starts a new origin.
  No C ABI layout, SDK fingerprint, persistence format or dependency is changed.

## Gamepad feedback

- Scoped C++ game requests can submit bounded low/high-frequency controller
  vibration through existing host/device and SDK Play effect owners.
- Real SDL return values determine receipts. Focus/routing/Play/device transitions
  stop feedback; unsupported controllers do not report success.
- The original public SDK example uses control actions and normal receipt release.
  No new C ABI layout, persisted data or dependency upgrade is introduced.

## Native ECS profiling

- Play Profiler shows the actual fixed-pipeline order, native merge points and
  cumulative system CPU totals, with current collection/Stop/Clear controls.
- Optional Play/automation/MCP observations share the same bounded runtime copy.
  Inspection never advances gameplay; native lifetime counters retain their meaning.
- Linux checks cover native order/counters, lifecycle, both Play hosts and MCP,
  and the real ImGui controls. Windows execution and authentic captures are queued.

## Analog input response

- Continuous analog bindings support an outer deadzone and power response exponent
  through Project Settings, saved input maps and normal gameplay/MCP observations.
- Radial sticks preserve direction; buttons and relative mouse retain their current
  mapping. Invalid effective ranges reject before settings publication.
- Rebinding clears incompatible processing. Windows/physical controller response
  review and an authentic updated settings capture remain queued.
