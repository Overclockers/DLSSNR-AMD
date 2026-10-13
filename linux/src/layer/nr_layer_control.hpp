#pragma once
#include "nr_runtime.hpp"
namespace nr {
// Submitted work counters, not a claim that a frame is visible on screen.
struct LayerStatus {
    uint64_t submitted_frames{}, control_revision{};
    uint32_t network_dispatches{};
    bool available{}, applied{};
    char reason[256]{};
};
// A separate entry point preserves the r43 getter's ABI and output size.
struct LayerStatusV2 {
    uint64_t submitted_frames{}, control_revision{};
    uint32_t network_dispatches{};
    uint32_t source_width{}, source_height{}, working_width{}, working_height{};
    uint64_t runtime_generation{};
    char accumulation[16]{};
    bool available{}, applied{};
    char reason[256]{};
};
}
extern "C" bool nrSetControls(VkDevice, const nr::Controls*);
extern "C" bool nrGetSwapchainStatusV2(VkDevice, VkSwapchainKHR, nr::LayerStatusV2*);
extern "C" bool nrSetAccumulation(VkDevice, const char*);
extern "C" bool nrGetSwapchainStatus(VkDevice, VkSwapchainKHR, nr::LayerStatus*);
