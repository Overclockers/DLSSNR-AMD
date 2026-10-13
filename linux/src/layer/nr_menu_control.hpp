#pragma once
#include "nr_layer_control.hpp"
namespace nr {
// Additive diagnostics ABI: actual item bounds in swapchain pixels. A missing
// item is not on the current page. It does not synthesize input or edit controls.
struct MenuStatus {
    bool visible{}, captured{};
    char reason[256]{};
};
struct MenuRect {
    float x0{}, y0{}, x1{}, y1{};
};
} // namespace nr
extern "C" bool nrGetControls(VkDevice, nr::Controls *);
extern "C" bool nrGetMenuStatus(VkDevice, VkSwapchainKHR, nr::MenuStatus *);
extern "C" bool nrGetMenuItemRect(VkDevice, VkSwapchainKHR, const char *, nr::MenuRect *);
