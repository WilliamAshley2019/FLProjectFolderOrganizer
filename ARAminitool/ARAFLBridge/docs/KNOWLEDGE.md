# What we know, what we are guessing, and the experiments that settle it

The bridge is only as good as our model of the .flp. Below, every assumption baked into the exporter, how wrong it could
be, and a one-variable experiment to fix it (the same single-variable-diff method your parser comments already use).
Run `flp2sidecar --probe` on each test file and read the `probe` section (raw playlist fields) plus `regions`.

| # | Assumption in the code | Confidence | Experiment |
|---|---|---|---|
| 1 | `itemIndex < patternBase` => channel IID; `>=` => pattern | High (matches ArrangementDumper) | n/a |
| 2 | Track index = `nTracks - 1 - trackRvidx` | High (same as ArrangementDumper) | Put one clip on track 1 and one on the last track |
| 3 | Audio clips are channels with a sample path | **Medium**: `ChannelType` has no "Audio Clip" value. Real FL audio clips may be Sampler-type channels or something else | Project with exactly one audio clip, nothing else. Check `skipped[].reason` and `regions[]` |
| 4 | Position/length are PPQ ticks, seconds = ticks * 60 / (bpm * ppq) | High for position, check length | Clip at bar 2 (expect 4 beats * ppq), 1 bar long |
| 5 | `startOffset` / `endOffset` units | **Unknown** (floats; beats? seconds? fraction?) | 4 s click track at 120 bpm. Trim head by exactly 1 beat, save, read `rawStartOffset`. Repeat trimming 2 beats, then trim the tail. Three points give the unit and scale |
| 6 | Tempo is constant (`Project::getTempo`) | Medium. `getTempoAutomationPoints()` returns the first Automation event's points, which may not be tempo | Project with a tempo automation clip: diff against the same project without it; find which event holds it |
| 7 | Time signature 4/4 | Low. TimeSig events have no position in the parser | Change signature mid-song via a marker; diff |
| 8 | `itemFlags`, `group` meaning | Unknown | Mute one clip, then lock another; diff `itemFlags` |
| 9 | Clip stretch / pitch (FL's own) | Unknown | Stretch one clip 2x in FL, diff the channel's event tree (look for IntStretch, StretchMode) |
| 10 | Sample paths relative/embedded | Medium | Save the same project with "Make paths relative" and compare `rawPath` |

Why this matters for FL's engine: items 5, 8 and 9 are exactly the per-clip state FL applies *before* audio reaches a mixer
insert. Once they are decoded, the bridge can either reproduce them in the ARA model (trim, stretch) or detect and warn
when FL's own processing would double up with the inner ARA plug-in.
