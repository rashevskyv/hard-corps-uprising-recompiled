#include "cheat_menu.h"

#include <cstddef>
#include <iterator>

#include <imgui.h>

#include <rex/ui/imgui_drawer.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>

#include "cheats.h"
#include "launcher.h"
#include "modal_dialog.h"

namespace cheats = hcu::cheats;

namespace hcu {
namespace {

// Above the ImGui drawer (64), so F10 is consumed before the drawer or the game see it.
constexpr size_t kInputZOrder = 1000;

const ImVec4 kAccent(0.36f, 0.88f, 0.44f, 1.0f);
const ImVec4 kPanel(0.04f, 0.08f, 0.07f, 0.92f);
const ImVec4 kRowFocus(0.09f, 0.36f, 0.22f, 0.95f);
const ImVec4 kDim(0.62f, 0.68f, 0.66f, 1.0f);

struct Row {
  cheats::Cheat cheat;
  const char* label;
};
constexpr Row kRows[] = {
    {cheats::Cheat::Health, "Infinite Health"},
    {cheats::Cheat::Lives, "Infinite Lives"},
    {cheats::Cheat::RapidFire, "Rapid Fire"},
    {cheats::Cheat::JumpHeight, "Jump Height 2x"},
    {cheats::Cheat::KeepWeapons, "Keep Weapons on Hit"},
    {cheats::Cheat::MovementSpeed, "Movement Speed 2x"},
    {cheats::Cheat::SuperArmor, "Super Armor"},
};
static_assert(std::size(kRows) == cheats::kCheatCount);

}  // namespace

class CheatMenuDialog final : public ModalDialog {
 public:
  CheatMenuDialog(rex::ui::ImGuiDrawer* drawer, CheatMenu& menu) : ModalDialog(drawer), menu_(menu) {}
  ~CheatMenuDialog() override { menu_.DialogClosed(); }

 protected:
  void DrawContent(ImGuiIO& io) override { DrawMenu(io); }

 private:
  void DrawMenu(ImGuiIO& io) {
    ImFont* regular = LauncherRegularFont();
    ImFont* strong = LauncherSemiboldFont() ? LauncherSemiboldFont() : regular;
    ImGui::PushFont(regular);
    const float em = ImGui::GetFontSize();
    const float margin = em * 0.8f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - margin, margin), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(em * 18.0f, 0.0f), ImGuiCond_Always);
    if (!focused_) {
      focused_ = true;
      ImGui::SetNextWindowFocus();
    }
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, kAccent);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kRowFocus);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, kRowFocus);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em * 0.7f, em * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##hcu_cheat_menu", nullptr, kFlags)) {
      ImGui::PushFont(strong);
      ImGui::TextColored(kAccent, "CHEATS");
      ImGui::PopFont();
      ImGui::Separator();
      for (size_t i = 0; i < std::size(kRows); ++i) {
        DrawRow(kRows[i], i);
      }
      ImGui::Separator();
      ImGui::TextColored(kDim, "F10 Close");
      ImGui::TextColored(kDim, "Not saved to the launcher");
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
  }

  void DrawRow(const Row& row, size_t index) {
    const bool on = cheats::Enabled(row.cheat);
    const float height = ImGui::GetFrameHeight() + 4.0f;
    ImGui::PushID(int(index));
    const bool pressed = ImGui::Selectable("##row", false, ImGuiSelectableFlags_None, ImVec2(0.0f, height));
    if (index == 0) {
      ImGui::SetItemDefaultFocus();
    }
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (ImGui::IsItemFocused()) {
      draw->AddRectFilled(min, max, ImGui::GetColorU32(kRowFocus), 3.0f);
    }
    const float pad = ImGui::GetFontSize() * 0.4f;
    const float y = min.y + (height - ImGui::GetFontSize()) * 0.5f;
    const char* state = on ? "On" : "Off";
    draw->AddText(ImVec2(min.x + pad, y), ImGui::GetColorU32(ImGuiCol_Text), row.label);
    draw->AddText(ImVec2(max.x - pad - ImGui::CalcTextSize(state).x, y), ImGui::GetColorU32(on ? kAccent : kDim),
                  state);
    ImGui::PopID();
    if (pressed) {
      cheats::ToggleForSession(row.cheat);
    }
  }

  CheatMenu& menu_;
  bool focused_ = false;
};

void CheatMenu::Attach(rex::ui::Window* window, rex::ui::ImGuiDrawer* drawer) {
  window_ = window;
  drawer_.store(drawer, std::memory_order_release);
  window_->AddInputListener(this, kInputZOrder);
}

void CheatMenu::Detach() {
  if (window_) {
    window_->RemoveInputListener(this);
    window_ = nullptr;
  }
  delete dialog_;
  drawer_.store(nullptr, std::memory_order_release);
}

bool CheatMenu::GameInputActive() const {
  if (ModalDialog::AnyOpen()) {
    return false;
  }
  // As ReXGlue does for its own overlays: a pointer over an overlay window keeps the click from the game.
  rex::ui::ImGuiDrawer* drawer = drawer_.load(std::memory_order_acquire);
  return !drawer || !drawer->HasDialogs() || !drawer->GetIO().WantCaptureMouse;
}

void CheatMenu::OnKeyDown(rex::ui::KeyEvent& e) {
  if (e.target() != window_ || e.virtual_key() != rex::ui::VirtualKey::kF10) {
    return;
  }
  e.set_handled(true);
  if (e.prev_state()) {
    return;
  }
  if (dialog_) {
    dialog_->RequestClose();
  } else if (rex::ui::ImGuiDrawer* drawer = drawer_.load(std::memory_order_acquire)) {
    if (!ModalDialog::AnyOpen()) {
      dialog_ = new CheatMenuDialog(drawer, *this);
    }
  }
}

void CheatMenu::DialogClosed() {
  dialog_ = nullptr;
}

}  // namespace hcu
