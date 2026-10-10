#pragma once

inline constexpr char kWindowTitle[] = "Hard Corps: Uprising";

namespace rex::ui {
class Window;
class WindowedAppContext;
}  // namespace rex::ui

void AttachWindowHost(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context);
void DetachWindowHost();
