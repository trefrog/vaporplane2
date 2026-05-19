# Non-imports

These are intentionally not transplanted from ictgHax:

- tape slowdown effect
- synth voice system
- GM program styling
- drum kit logic
- world/wind/heat audio logic
- NPC/game turn logic
- old SDL2 app shell
- any effect code that is not stable and directly needed

Slowdown will be rebuilt later as simple sample playback rate:
`playback_rate < 1.0`, with pitch following speed by default.