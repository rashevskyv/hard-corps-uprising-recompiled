# Console speed: what the 0.160 console log shows (2026-10-27)

Source: `hcu_log.txt` of the user's run of 0.160.6fc8ede on the console (`E:\conker-data\hub-logs\2026-10-27\`).

| Part | fps | Backend `draws` per frame | What the threads do |
| --- | --- | --- | --- |
| Intro, title, menus | 47-60 | 0.1-0.8 ms | |
| Stage 1 | 12-14 (median over the run 13.8) | **63-66 ms** | Main XThread 99% of a core; GPU Commands only 8-9% of a core, 92% of its PC samples in one kernel stub (blocked); Audio Worker (the old silent driver) 70-72% of another core |

## Cause 1: the backend log flushed the SD card on every line

`Log()` ended in `fflush(stdout)`. On the console stdout is the SD log; the host layer buffers it and flushes
twice a second on purpose ("each flush costs"). A flush per line is a filesystem call that blocks the
command thread for a millisecond or more. In the stage the backend wrote 21,162 "image recreated" and
17,196 "image resized" lines in a few minutes, so the thread was blocked about 65 ms per frame while using
little CPU. Fixed in 0.168: no per-line flush on the Switch, the image lines are capped at 40, and the
60-swap summary shows the number of image flips.

## Cause 2 (not fixed yet): about 30 guest images flip format or size every frame

Per frame, the same guest addresses are asked for two formats (28 = RGBA8 colour resolve, 52 = a depth
resolve) and two sizes (aligned resolve size vs exact texture size: 480x272 vs 480x270, 64x40 vs 60x34). Each
flip makes a new image, clears it and copies the old one. Plan: keep one image per (address, format) and one
exact-size copy per resolve, recopied only when the resolve wrote again.

## Cause 3 (fixed in 0.163): the Audio Worker spun at 70% of a core

The silent audio driver of the nop audio system polled; the libnx audout driver (from gh-recomp) is paced
by the device.
