# Tap Diagnostic for Pebble

A diagnostic app to test accelerometer tap detection on Pebble watches.
Built to investigate reports of `accel_tap_service` not working on PebbleOS v4.9.171.

## What it tests

- **Firmware tap service** (`accel_tap_service_subscribe`) — counts events from the OS-level double-tap detection (BMI160 hardware interrupt)
- **Raw accel data** (`accel_data_service_subscribe`) — confirms accelerometer data is flowing at all
- **Custom spike detection** — detects acceleration deltas above various thresholds (800, 1200, 1800, 2500 mG)
- **Peak tracking** — records the highest acceleration spike seen

## Screens

1. **Live** — Real-time accel values, tap counts, peak
2. **Log** — Event history showing when taps/spikes were detected
3. **Summary** — Screenshot-friendly report to share

## Controls

- **UP**: Cycle screens
- **DOWN**: Reset all counters
- **BACK**: Exit

## How to test

1. Install and open the app
2. Wait 2-3 seconds for data to start flowing
3. Try: single tap, double tap, wrist flick, hard tap
4. Navigate to Summary screen (UP twice)
5. Take a screenshot and share the results

## What the results mean

| Result | Meaning |
|--------|---------|
| Accel data: NO | Accelerometer service not delivering data — firmware issue |
| FW tap: 0, Raw tap: >0 | Firmware tap detection broken, but accel works — use raw detection |
| FW tap: 0, Raw tap: 0, Peak < 800 | Taps aren't generating enough force, or accel is filtered |
| FW tap: 0, Raw tap: 0, Peak > 1800 | Spikes detected but cooldown filtering them — adjust threshold |
| Both counts > 0 | Everything works! |

## Background

The Pebble Time 2 (platform "robert") uses a BMI160 IMU with hardware double-tap detection.
The firmware configures: quiet=20-30ms, shock=50-75ms, duration=300ms, threshold=12500mG.
This diagnostic helps determine if the issue is hardware interrupt config, data flow, or threshold sensitivity.
