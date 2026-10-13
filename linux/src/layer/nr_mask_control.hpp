#pragma once
#include "nr_runtime.hpp"
namespace nr {
// Additive API; existing Controls and LayerStatus ABI remain unchanged.
struct ControlMaskStatus {
    bool supported{}, bound{}, enabled{}, consumed{}, applied{};
    uint32_t width{}, height{};
    uint64_t binding_revision{}, submitted_frames{};
    char reason[256]{};
};
}
// Register a persistent, adapter-provided linear RGBA32F mask for this swapchain.
// Not a segmentation generator. Caller guarantees image format/usage/ownership
// per ControlMaskFrame and keeps image + memory alive until successful unbind
// (frame=nullptr), replacement, or swapchain destruction. Replacement/unbind
// waits prior NR reads. All writes must precede Present on the SAME queue;
// caller externally serializes that queue, including registration and Present.
// before==after is required: GENERAL or SHADER_READ_ONLY_OPTIMAL each frame.
// The pointer/descriptor is copied; the VkImage and its memory remain borrowed.
extern "C" bool nrBindControlMask(VkDevice,VkSwapchainKHR,VkQueue,const nr::ControlMaskFrame*);
extern "C" bool nrSetExternalMaskEnabled(VkDevice,bool);
extern "C" bool nrGetControlMaskStatus(VkDevice,VkSwapchainKHR,nr::ControlMaskStatus*);
