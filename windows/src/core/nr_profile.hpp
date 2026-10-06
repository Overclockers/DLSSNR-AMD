#pragma once
// Diagnostic-only markers. A caller-owned query range belongs to one submission
// and must not be reset or read until that submission's fence has completed.
// Indices: upload begin/end, core input begin, conversion end, encode end,
// operators begin/end, composition end, core end, download end.
// Operator markers require a single active pass; composition requires apply_model.
#define NR_PROFILE_COUNT 10u
#ifdef NR_DIAGNOSTICS
#include <vulkan/vulkan.h>
namespace nr {
inline thread_local VkQueryPool profile_pool{};
inline thread_local uint32_t profile_base{};
struct ProfileScope {
    VkQueryPool saved_pool{profile_pool};
    uint32_t saved_base{profile_base};
    ProfileScope(VkQueryPool pool, uint32_t base) { profile_pool = pool; profile_base = base; }
    ~ProfileScope() { profile_pool = saved_pool; profile_base = saved_base; }
};
inline void profile_mark(VkCommandBuffer cmd, uint32_t index) {
    if (profile_pool)
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, profile_pool, profile_base + index);
}
}
#define NR_PROFILE_MARK(cmd, index) nr::profile_mark(cmd, index)
#else
#define NR_PROFILE_MARK(cmd, index) ((void)0)
#endif
