# Display

## Window

- Starts windowed: 853x480 logical, centred on monitor 1 (`config/hard_corps_uprising.toml`). The guest video mode stays 1280x720.
- **F11** toggles Borderless Fullscreen. It is SDL's default fullscreen mode, not an exclusive display mode, and it uses the current display.
- F11 again returns to windowed. SDL restores the previous windowed size and position.
- Auto-repeat is ignored, so holding F11 toggles once.
- ReXGlue's `Window::SetFullscreen` discards SDL's return value, so a refused switch is not reported.

## Title FPS

- Title: `Hard Corps: Uprising | FPS: 60.00`, refreshed once per second.
- Shows the plain title until the first frame is counted.
- Shown in windowed mode. Still counted in fullscreen, where the title bar is hidden.
- Source: a pass-through hook on the title's swap routine `sub_825520F8`, the same kind of hook Earthworm uses for its FPS counter. The original runs first, unchanged, and the hook only counts the call.
- Limitation: that routine skips its `VdSwap` call while `obj+21748` is zero. The count can therefore exceed the number of frames the GPU presented. Read the value as an approximation of the game's frame rate.
- No frame limit, VSync or timing change.
