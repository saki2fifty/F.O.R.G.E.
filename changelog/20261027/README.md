# 2026-10-27 — Runtime timing and gamepad feedback

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
