#pragma once
#include "nr_layer_control.hpp"
#include "nr_profile.hpp"
#include "nr_mask_control.hpp"
#include "nr_menu_control.hpp"
#include <memory>
#include <vector>
struct ImGuiIO;
namespace nr {
class MenuInput {
  public:
    MenuInput(const std::string &display, uint32_t window);
    // Wayland: no display to connect to, input arrives from the application
    // through the SDL3 bridge. See the comment at the top of nr_menu_input.cpp.
    MenuInput();
    ~MenuInput();
    void update(ImGuiIO &, bool &nr_enabled);
    // The application's own word that its window has the focus, for a swap chain window the X focus cannot be
    // related to (Wine may give a child window's Vulkan surface an X window of its own).
    void set_focus_hint(bool focused);
    void close();
    bool visible() const;
    bool captured() const;
    const std::string &status() const;

  private:
    void update_bridge(ImGuiIO &, bool &nr_enabled);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class Menu {
  public:
    Menu(const HostDevice &, VkFormat, VkExtent2D, const std::vector<VkImage> &, std::shared_ptr<MenuInput>);
    ~Menu();
    // Caller has waited this acquired image's submit fence. Rendering goes after
    // NR so UI never enters the network. All ImGui contexts share an internal lock.
    bool frame(Controls &, std::string &policy, Profile &, const LayerStatusV2 &, const std::string &reason, const ControlMaskStatus &mask = {});
    void record(VkCommandBuffer, uint32_t image_index);
    bool rect(const char *, MenuRect &) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nr
