# Porting Xbox 360 games to Nintendo Switch (and Linux): knowledge base

Static recompilation of Xbox 360 (Xenon PowerPC) XBLA games with the ReXGlue SDK and our own deko3d renderer.
Everything here comes from real work on Castlevania: Harmony of Despair, Hard Corps: Uprising and Guardian Heroes. No game data is included.

Start with `deko3d-and-field-lessons.md` (what went wrong and how it was fixed), then read what the task needs:

| File | Content |
|---|---|
| `deko3d-and-field-lessons.md` | Why deko3d, Xenos translation bugs, memory, priorities, workflow, legal |
| `pipeline.md` | XEX to codegen: toolchains, manifest keys, hard problems and fixes |
| `performance.md` | Decision tree for slow ports, codegen flags, spins and waits, host bugs |
| `runtime-kernel.md` | Kernel exports, time, CPU semantics, audio/XMA, input |
| `gpu.md` | Emulate vs native renderer, shaders, formats, EDRAM and resolve, 2D checklist |
| `switch-horizon.md` | Prior-art Switch ports, memory model, threads and priorities, SD, exit |
| `titles.md` | Decision list for the XBLA titles we looked at |
| `projects.md` | Catalogue of other repos and what was actually checked |

A Claude skill built on these files lives in `.claude/skills/xbox360-recomp/` (its `SKILL.md` points here).
Tags used in the files: [CODE] source read, [DOC] doc only, [GUESS] inference, [HW] measured on the console, [EDEN] seen in Eden, [LINUX] seen on Linux.
