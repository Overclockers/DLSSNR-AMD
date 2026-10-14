#include "nr_pe_session.hpp"
#include "nr_pe_bridge.hpp"
#include "nr_pe_d3d12split.hpp"
#if NR_INT4
#include "nr_pe_config.hpp"   // Int4Switch
#endif
#include "nr_pe_log.hpp"
#include "nr_pe_vkdevice.hpp"
#include "nr_pipeline_binary.hpp"
#include "nr_log.hpp"
#include "nr_screenshot.hpp"
#include <windows.h>
#include <d3d10.h>
#include <d3d11_4.h>
#include <dxgi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace nr::pe {
namespace {

using nr::pe::log;

// What the process has left of its own address space.
//
// This exists because of one log line: `neural rendering unavailable:
// std::bad_alloc`, from a 32-bit game at 4K with 11979 MB of VRAM free, 482 ms
// into a build - and nothing else. A `std::bad_alloc` is `operator new`
// refusing, which in a 32-bit process almost always means the 2 GB of virtual
// address space is gone, not that the machine is out of memory; a Vulkan
// failure would have come back through NRVK_CHECK with a VkResult instead. The
// two numbers that tell those apart are the total free and the **largest
// contiguous free block** - a 300 MB request fails against 900 MB of free space
// scattered in 60 MB pieces, and only the second number shows it.
//
// A 64-bit process has terabytes and this reads as such; the walk costs one
// VirtualQuery per region either way, and it is only ever called on a failure.
std::string address_space_report() {
    MEMORY_BASIC_INFORMATION region{};
    unsigned long long free_total = 0, largest = 0;
    auto* address = static_cast<unsigned char*>(nullptr);
    while (VirtualQuery(address, &region, sizeof region) == sizeof region) {
        if (region.State == MEM_FREE) {
            free_total += region.RegionSize;
            if (region.RegionSize > largest) largest = region.RegionSize;
        }
        auto* next = static_cast<unsigned char*>(region.BaseAddress) + region.RegionSize;
        if (next <= address) break;   // no progress, or the pointer wrapped
        address = next;
    }
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof memory;
    GlobalMemoryStatusEx(&memory);
    char text[256];
    std::snprintf(text, sizeof text,
                  "address space: %.0f MB free in this process, largest contiguous block %.0f MB "
                  "(of %.0f MB total); %.0f MB of RAM free",
                  double(free_total) / (1 << 20), double(largest) / (1 << 20),
                  double(memory.ullTotalVirtual) / (1 << 20),
                  double(memory.ullAvailPhys) / (1 << 20));
    return text;
}

// What the card and the machine have left, in bytes.
//
// **Both, because on Windows the network is charged twice.** WDDM backs every
// video-memory allocation with system commit (RAM plus page file), so the 3.9
// GB arena of a 3818x1998 build takes 3.9 GB of VRAM budget *and* 3.9 GB of
// commit. A tester's machine ran out of the second with VRAM to spare (package
// test 4: 43286 of 43391 MB committed, the game holding 20.8 GB): DXVK's
// vkEndCommandBuffer failed, Dalamud wrote through a null pointer and the
// desktop lost its icons. Earlier "out of VRAM" failures - DXVK refused 108 MB
// with 2.3 GB of budget left, pipelines failed with OUT_OF_HOST_MEMORY - were
// the same thing.
struct MemoryState {
    uint64_t vram_used{}, vram_budget{}, vram_free{};   // largest device-local heap
    uint64_t commit_free{}, commit_limit{};             // this process's view of system commit
    bool vram_known{};
};

MemoryState read_memory(VkPhysicalDevice physical) {
    MemoryState m;
    if (physical) {
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
        VkPhysicalDeviceMemoryProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
        props.pNext = &budget;
        vkGetPhysicalDeviceMemoryProperties2(physical, &props);
        // The largest device-local heap's free space, by budget where the driver
        // reports one and by heap size where it does not.
        const auto& mem = props.memoryProperties;
        for (uint32_t i = 0; i < mem.memoryHeapCount; ++i) {
            if (!(mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)) continue;
            const uint64_t cap = budget.heapBudget[i] ? budget.heapBudget[i] : mem.memoryHeaps[i].size;
            const uint64_t used = budget.heapUsage[i];
            const uint64_t free = cap > used ? cap - used : 0;
            if (!m.vram_known || free > m.vram_free) {
                m.vram_free = free; m.vram_used = used; m.vram_budget = cap; m.vram_known = true;
            }
        }
    }
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof memory;
    if (GlobalMemoryStatusEx(&memory)) {
        m.commit_free = memory.ullAvailPageFile;
        m.commit_limit = memory.ullTotalPageFile;
    }
    return m;
}

double mb(uint64_t bytes) { return double(bytes) / (1 << 20); }

// Headroom left for the game after a build, and the levels at which a running
// network steps aside. The gap between the two is the hysteresis that keeps a
// build from being made and dropped on alternate seconds.
constexpr uint64_t kVramReserve = 1024ull << 20, kCommitReserve = 3072ull << 20;
constexpr uint64_t kVramLow = 384ull << 20, kCommitLow = 1536ull << 20;

}  // namespace

// What the bridge needs to know about one D3D12 frame, whichever route handed it over.
struct BridgeInputs {
    ID3D12Resource* target{};
    D3D12_RESOURCE_STATES target_state{};
    // Where the colour is read from when it is not `target` itself (same format and extent).
    ID3D12Resource* source{};
    D3D12_RESOURCE_STATES source_state{};
    ID3D12Resource* motion{};
    D3D12_RESOURCE_STATES motion_state{};
    ID3D12Resource* depth{};
    D3D12_RESOURCE_STATES depth_state{};
    Session::Subrect motion_subrect{}, depth_subrect{};
    float motion_scale_x{1.0f}, motion_scale_y{1.0f};
    bool depth_inverted{true};
    bool reset{};
    uint64_t feature{};
    bool linear{};   // the colour is scene-referred linear light (encoded by the runtime)
    // The RenoDX-style option: its processing around the network instead (Session::run_renodx), with the
    // game's exposure texture when the route has one.
    const Session::RenoDXFrame* renodx{};
    ID3D12Resource* exposure{};
    D3D12_RESOURCE_STATES exposure_state{};
};

// The RenoDX-style option on the bridge: the stage recorded in the job in place of the network, on the shared
// images (in place: the copy out takes its answer).
struct BridgeRenoDX {
    renodx::Stage* stage{};
    Controls controls{};
    const Session::RenoDXFrame* frame{};
    bool exposure{};   // the shared exposure image holds the game's exposure texture
};

struct Session::Impl {
    std::string root;
    std::string status;
    // ---- one built network per extent ----------------------------------------
    //
    // There used to be exactly one, rebuilt whenever anything about it changed.
    // OptiScaler keeps neural-rendering features alive at *two* extents at once
    // - pre-upscale at the render resolution and post-upscale at the display
    // one - and creates and destroys them on every toggle, so a single slot
    // thrashed: 1920x1080, then 3840x2160, then 1920x1080 again, a full rebuild
    // each time, with frames passing through unenhanced in between. That is the
    // stall the user reported and it is also why "passes" appeared to do
    // nothing: the network was never finished long enough to run.
    //
    // So built networks are kept, keyed by everything that makes one different,
    // and a switch between two of them is a pointer assignment. They are
    // evicted least-recently-used, and only when the card cannot hold the next
    // one (fits_in_memory) - the arena is 1.05 GB at 1080p and 4.07 GB at 4K, so
    // this is not a cache that can be allowed to grow.
    struct Built {
        std::unique_ptr<Runtime> runtime;
        uint32_t width{}, height{};
        VkFormat format{VK_FORMAT_UNDEFINED};
        bool linear{};
        float scale{1.0f};
        // The pass ceiling this one was built for: it sizes one history image
        // per pass, so a network built for four cannot serve a request for six.
        uint32_t passes{4};
        bool prep{};   // RuntimeConfig::preprocess
        bool native{}; // RuntimeConfig::native_compose
#if NR_INT4
        int int4{};    // RuntimeConfig::int4_mixed asked for (0 default, 1 int4 mixed)
#endif
        uint64_t used{};   // LRU stamp
        std::chrono::steady_clock::time_point last{};   // when it was last selected
    };
    // A cached network taken up again after serving nothing for this long starts its features fresh (as after a
    // Reset), so no frame is blended with a history that old; RenoDX's own look history uses the same bound.
    static constexpr std::chrono::milliseconds kStaleHistory{250};
    std::vector<Built> cache;
    uint64_t use_stamp{};
    // The RenoDX-style option: one stage per feature, made at the frame's extent, format and HDR judgement.
    // A replaced or released one is kept for kRetireCalls more evaluations and kRetireTime, as networks are.
    struct RenoDXEntry {
        std::unique_ptr<renodx::Stage> stage;
        uint32_t w{}, h{}, nw{}, nh{}, present{};
        VkFormat format{VK_FORMAT_UNDEFINED};
        bool hdr{};
        // RenoDX's CommitNrResolution: the network's extent in use, and one the game's guides moved to waiting
        // out kContractDebounceFrames (a settings change is taken at once).
        uint32_t applied_w{}, applied_h{}, pending_w{}, pending_h{}, pending_frames{};
        float scale{-1.0f};
        bool follow{};
        uint32_t stack{1};   // the model passes its stack last ran: pass k > 0 keeps a history of its own
    };
    std::map<uint64_t, RenoDXEntry> renodx;
    // The history pass k of a feature's stack runs on (pass 0: the feature's own).
    static uint64_t stack_feature(uint64_t feature, uint32_t k) {
        return k == 0 ? feature : (uint64_t(1) << 62) | (feature << 8) | k;
    }
    // Pass 1's name on a route whose frames carry none (feature 0), when it stacks; no game feature is numbered so.
    static constexpr uint64_t kUnnamedStack = 0xFFFFFFu;
    void release_everywhere(uint64_t feature) {
        for (auto& e : cache) e.runtime->release_feature(feature);
    }
    // RenoDX's neutral guides (NRPresentGuides): zero motion vectors at the frame's extent, for a Present-path frame
    // without the game's (Optional) or always (Never). Made and cleared on first use, kept.
    VkImage neutral_mv{};
    VkDeviceMemory neutral_mv_memory{};
    uint32_t neutral_w{}, neutral_h{};
    bool neutral_cleared{};
    bool neutral_motion(VkCommandBuffer cmd, uint32_t w, uint32_t h, ColourFrame* out);
    // Whether a RenoDX frame of this route runs without the game's motion vectors (the Present path's Optional and
    // Never; Required, and every other route, need them).
    static bool renodx_needs_motion(const Session::RenoDXFrame& rf) {
        return !rf.present || rf.settings.present_guides == 1u;
    }
    // The RenoDX-style option's cost for NR Control and the log: a timestamp pair around the whole stage and one
    // around each pass's network, in the same frame, a ring of frames read back without waiting, averaged as the
    // runtime averages its own (the runtime's own timing covers one pass at a time).
    static constexpr uint32_t kStageSlots = 8, kStageQueries = 2 + 2 * renodx::kMaxUnlockedPasses;
    VkQueryPool stage_timing{};
    bool stage_timing_tried{};
    double stage_timestamp_ns{};
    uint32_t stage_slot{};
    uint32_t stage_slot_passes[kStageSlots]{};
    uint64_t stage_recorded{};
    float stage_ms_avg{}, stage_net_ms_avg{};
    bool renodx_timed{};
    void read_stage_timing() {
        if (stage_recorded <= kStageSlots) return;
        const uint32_t n = 2 + 2 * stage_slot_passes[stage_slot];
        uint64_t stamps[kStageQueries] = {};
        if (vkGetQueryPoolResults(handles.device, stage_timing, stage_slot * kStageQueries, n, n * sizeof(uint64_t), stamps,
                                  sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS || stamps[1] <= stamps[0])
            return;   // still running: next frame
        uint64_t net = 0;
        for (uint32_t p = 0; p < stage_slot_passes[stage_slot]; ++p)
            if (stamps[3 + 2 * p] > stamps[2 + 2 * p]) net += stamps[3 + 2 * p] - stamps[2 + 2 * p];
        const float ms = float(double(stamps[1] - stamps[0]) * stage_timestamp_ns / 1e6);
        const float net_ms = float(double(net) * stage_timestamp_ns / 1e6);
        const bool first = stage_ms_avg <= 0.0f;
        stage_ms_avg = first ? ms : stage_ms_avg + (ms - stage_ms_avg) * 0.1f;
        stage_net_ms_avg = first ? net_ms : stage_net_ms_avg + (net_ms - stage_net_ms_avg) * 0.1f;
    }
    bool stage_timing_ready() {
        if (stage_timing_tried) return stage_timing != VK_NULL_HANDLE;
        stage_timing_tried = true;
        uint32_t n = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(handles.physical, &n, nullptr);
        std::vector<VkQueueFamilyProperties> families(n);
        vkGetPhysicalDeviceQueueFamilyProperties(handles.physical, &n, families.data());
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(handles.physical, &props);
        if (access.family >= n || !families[access.family].timestampValidBits || props.limits.timestampPeriod <= 0.0f) return false;
        stage_timestamp_ns = props.limits.timestampPeriod;
        VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = kStageSlots * kStageQueries;
        if (vkCreateQueryPool(handles.device, &qi, nullptr, &stage_timing) != VK_SUCCESS) stage_timing = VK_NULL_HANDLE;
        return stage_timing != VK_NULL_HANDLE;
    }
    // RenoDX's NR screenshot (nr_screenshot.hpp): one capture's two readbacks in one host-visible buffer (the image
    // before NR at 0, after it at `bytes`), filled by copies recorded in the frame's own command buffer; an event set
    // after them is the proof the GPU ran them (an event starts unset; a new query pool's queries are undefined until
    // the command buffer resetting them has run, and RADV reads them as available). Handed to the worker once it has,
    // freed once the worker has read it; a capture never proven run is kept until the session goes (its command buffer
    // may still run).
    struct Shot {
        VkBuffer buffer{};
        VkDeviceMemory memory{};
        void* mapped{};
        uint64_t bytes{};
        VkEvent done{};
        screenshot::Pair pair;
        uint32_t frames{};
        bool handed{};
        std::atomic_bool read{false};
    };
    static constexpr uint32_t kShotSettleFrames = 240;   // RenoDX's kExactSettlePresents
    std::unique_ptr<Shot> shot;                          // recorded, waiting for the GPU
    std::vector<std::unique_ptr<Shot>> shots_out;        // with the worker, or kept
    void shot_free(Shot& sh) {
        if (sh.mapped) vkUnmapMemory(handles.device, sh.memory);
        if (sh.buffer) vkDestroyBuffer(handles.device, sh.buffer, nullptr);
        if (sh.memory) vkFreeMemory(handles.device, sh.memory, nullptr);
        if (sh.done) vkDestroyEvent(handles.device, sh.done, nullptr);
        sh.mapped = nullptr; sh.buffer = VK_NULL_HANDLE; sh.memory = VK_NULL_HANDLE; sh.done = VK_NULL_HANDLE;
    }
    // Readbacks for a w x h pair of `format`, stamped (nr_screenshot.hpp); null when anything fails.
    std::unique_ptr<Shot> shot_prepare(uint32_t w, uint32_t h, VkFormat format) {
        auto sh = std::make_unique<Shot>();
        sh->bytes = uint64_t(w) * h * screenshot::bytes_per_pixel(format);
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = sh->bytes * 2; bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VkMemoryRequirements req{};
        VkPhysicalDeviceMemoryProperties mem{};
        uint32_t type = ~0u;
        const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        VkEventCreateInfo ei{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
        if (!sh->bytes || vkCreateBuffer(handles.device, &bi, nullptr, &sh->buffer) != VK_SUCCESS) goto fail;
        vkGetBufferMemoryRequirements(handles.device, sh->buffer, &req);
        vkGetPhysicalDeviceMemoryProperties(handles.physical, &mem);
        for (int pass = 0; pass < 2 && type == ~0u; ++pass)   // host-cached first: the worker reads it all
            for (uint32_t i = 0; i < mem.memoryTypeCount && type == ~0u; ++i) {
                const VkMemoryPropertyFlags f = mem.memoryTypes[i].propertyFlags;
                if ((req.memoryTypeBits & (1u << i)) && (f & want) == want &&
                    (pass == 1 || (f & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)))
                    type = i;
            }
        if (type == ~0u) goto fail;
        {
            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.allocationSize = req.size; ai.memoryTypeIndex = type;
            if (vkAllocateMemory(handles.device, &ai, nullptr, &sh->memory) != VK_SUCCESS ||
                vkBindBufferMemory(handles.device, sh->buffer, sh->memory, 0) != VK_SUCCESS ||
                vkMapMemory(handles.device, sh->memory, 0, VK_WHOLE_SIZE, 0, &sh->mapped) != VK_SUCCESS) {
                sh->mapped = nullptr;
                goto fail;
            }
        }
        if (vkCreateEvent(handles.device, &ei, nullptr, &sh->done) != VK_SUCCESS) goto fail;
        screenshot::stamp(sh->mapped, sh->bytes);
        screenshot::stamp(static_cast<uint8_t*>(sh->mapped) + sh->bytes, sh->bytes);
        sh->pair.width = w; sh->pair.height = h; sh->pair.format = format;
        sh->pair.row_pitch = w * screenshot::bytes_per_pixel(format);
        sh->pair.bytes = sh->bytes;
        sh->pair.pre = sh->mapped;
        sh->pair.post = static_cast<uint8_t*>(sh->mapped) + sh->bytes;
        return sh;
    fail:
        shot_free(*sh);
        return nullptr;
    }
    // `image` (in `layout`, last used by `stage` / `access`) into the readback at `offset`, and back to `layout`.
    static void shot_copy(VkCommandBuffer cmd, Shot& sh, VkImage image, VkImageLayout layout, VkPipelineStageFlags stage,
                          VkAccessFlags access, VkDeviceSize offset) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.oldLayout = layout; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcAccessMask = access; b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, stage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {sh.pair.width, sh.pair.height, 1};
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sh.buffer, 1, &region);
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; b.newLayout = layout;
        b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; b.dstAccessMask = access;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, stage | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &b);
    }
    // Both copies recorded: the proof after them, and the capture is no longer armed.
    void shot_recorded(VkCommandBuffer cmd, std::unique_ptr<Shot> sh) {
        VkBufferMemoryBarrier bb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        bb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; bb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        bb.srcQueueFamilyIndex = bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bb.buffer = sh->buffer; bb.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &bb, 0, nullptr);
        vkCmdSetEvent(cmd, sh->done, VK_PIPELINE_STAGE_TRANSFER_BIT);
        screenshot::recorded();
        shot = std::move(sh);
    }
    // Once a frame: a recorded capture the GPU has run goes to the worker; one the worker has read is freed.
    void shot_poll() {
        for (auto it = shots_out.begin(); it != shots_out.end();)
            if ((*it)->handed && (*it)->read.load(std::memory_order_acquire)) { shot_free(**it); it = shots_out.erase(it); }
            else ++it;
        if (!shot) return;
        if (vkGetEventStatus(handles.device, shot->done) == VK_EVENT_SET) {
            Shot* sh = shot.get();
            sh->handed = true;
            sh->pair.owner = sh;
            sh->pair.release = [](void* owner) { static_cast<Shot*>(owner)->read.store(true, std::memory_order_release); };
            screenshot::Pair pair = sh->pair;
            shots_out.push_back(std::move(shot));
            screenshot::deliver(std::move(pair));
        } else if (++shot->frames > kShotSettleFrames) {
            screenshot::abandon("gpu_unconfirmed", "the command buffer carrying the copies did not run within 240 frames "
                                "(buffers kept)");
            shots_out.push_back(std::move(shot));
        }
    }
    struct RenoDXRetired { std::unique_ptr<renodx::Stage> stage; uint64_t call; std::chrono::steady_clock::time_point at; };
    std::vector<RenoDXRetired> renodx_retired;
    void retire_stage(std::unique_ptr<renodx::Stage> stage) {
        if (stage) renodx_retired.push_back({std::move(stage), calls, std::chrono::steady_clock::now()});
    }
    bool renodx_runtime(const Controls& controls, uint32_t w, uint32_t h, Controls* c);
    void renodx_extent(uint64_t feature, uint32_t w, uint32_t h, uint32_t guide_w, uint32_t guide_h,
                       const renodx::Settings& settings, uint32_t* nw, uint32_t* nh);
    renodx::Stage* renodx_stage(uint64_t feature, uint32_t w, uint32_t h, uint32_t nw, uint32_t nh, VkFormat format,
                                const Session::RenoDXFrame& rf);
    bool record_renodx(VkCommandBuffer cmd, renodx::Stage& stage, const ColourFrame& colour, const ColourFrame& output,
                       EngineFrame engine, const Controls& c, const Session::RenoDXFrame& rf,
                       const ColourFrame* exposure = nullptr);
    void free_retired_stages() {
        const auto now = std::chrono::steady_clock::now();
        for (size_t i = renodx_retired.size(); i-- > 0;)
            if (calls - renodx_retired[i].call > kRetireCalls && now - renodx_retired[i].at > kRetireTime)
                renodx_retired.erase(renodx_retired.begin() + std::ptrdiff_t(i));
    }
    // The entry in `cache` the run paths are using. Raw, because the cache owns
    // them; null until the first build lands.
    Runtime* runtime{};
    uint32_t width{}, height{};
    VkFormat format{VK_FORMAT_UNDEFINED};
    DeviceHandles handles{};
    // Our own render-resolution image, handed to the upscaler in place of the
    // game's colour. Owned as a D3D12 resource so the upscaler can take it, and
    // used as a VkImage so the network can write it - the same allocation seen
    // from both sides, which is the whole advantage of being inside vkd3d.
    ID3D12Resource* output{};
    ResourceHandle output_vk{};
    bool failed{};   // one hard failure disables the session rather than retrying into a crash
    bool typeless_noted{};
    float history_strength{1.0f};

    // A Vulkan game has no D3D12 allocation to borrow, so the output is ours.
    // Our own command buffer, for the paths that are not handed one.
    VkCommandPool pool{};
    VkCommandBuffer own_cmd{};
    // Building the runtime uploads the weights, which is a submit, not a
    // recording. Every path therefore has to find a queue before it can build
    // anything - the earlier code passed VK_NULL_HANDLE here and the runtime
    // rejects that outright, so no path had ever built a network.
    QueueAccess access{};
    ID3D12CommandQueue* owned_queue{};
    // DXVK's interop device, held for the life of the session: the queue lock
    // closures call into it every frame.
    IDXGIVkInteropDevice* dxvk{};

    VkImage vk_output{};
    VkImageView vk_output_view{};
    VkDeviceMemory vk_output_memory{};
    uint32_t vk_w{}, vk_h{};
    VkFormat vk_format{VK_FORMAT_UNDEFINED};

    // The one thing vkd3d-proton cannot be told any other way: that the compute
    // bind point of its command buffer no longer holds the pipeline it thinks it
    // does. See `forget_bound_pipeline` below for the whole story; these three
    // objects exist only to give it a ClearUAV to run.
    ID3D12Resource* invalidator{};
    ID3D12DescriptorHeap* invalidator_cpu_heap{};
    ID3D12DescriptorHeap* invalidator_gpu_heap{};
    bool invalidator_failed{};
    bool ensure_invalidator(ID3D12Device* device);
    void forget_bound_pipeline(ID3D12Device* device, ID3D12GraphicsCommandList* list);
    void release_invalidator();

    bool ensure_output(ID3D12Device* device, uint32_t w, uint32_t h, DXGI_FORMAT dxgi);
    void release_output();
    bool vk_output_valid(uint32_t w, uint32_t h, VkFormat format) const {
        return vk_output && vk_w == w && vk_h == h && vk_format == format;
    }
    bool ensure_vk_output(uint32_t w, uint32_t h, VkFormat format);
    void release_vk_output();
    // `linear`: the colour is scene-referred linear light (an upscaler's input
    // in a float format), so the runtime encodes it; false for every swapchain.
    // Builds in the background: returns false, with `status` saying so, until
    // the network is ready, and the caller leaves the frame alone meanwhile.
    // `controls` asking for a preprocess makes every runtime from then on one
    // that can run it (prep_want); see RuntimeConfig::preprocess.
    bool ensure_runtime(const Controls& controls, uint32_t w, uint32_t h, VkFormat format, bool linear);
    bool ensure_runtime_(const Controls& controls, uint32_t w, uint32_t h, VkFormat format, bool linear);
    // The preprocess meter in the log every five seconds while it runs, so NR on/off at one spot
    // can be compared (the feedback loop that made upstream drop frame metering).
    std::chrono::steady_clock::time_point meter_logged{};
    static bool is_linear_format(VkFormat f) {
        return f == VK_FORMAT_R16G16B16A16_SFLOAT || f == VK_FORMAT_B10G11R11_UFLOAT_PACK32 ||
               f == VK_FORMAT_R32G32B32A32_SFLOAT ||
               // Taken only through the rest-of-DXGI colour formats
               // (nr_pe_interop.cpp colour_format_fallback), so no frame that
               // ran before changes its answer here.
               f == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 || f == VK_FORMAT_R32G32B32_SFLOAT;
    }
    // The background build. Everything the thread needs is copied in; it hands
    // back either a runtime or an error under `build_lock`, and the render
    // thread adopts whichever on its next call.
    // Evicted networks wait here before they are destroyed. run_after and run_present record the network into
    // the game's own command list (vkd3d-proton's command buffer), which the game submits and the GPU runs frames
    // later: a network that served the previous frame can still be read by the GPU when its replacement is
    // adopted (the [Int4Mixed] switch, a resolution change). Destroying it then frees memory under in-flight work
    // and hangs the GPU. Freed after kRetireCalls more evaluations and kRetireTime, far past any frame in flight.
    struct Evicted { std::unique_ptr<Runtime> runtime; uint64_t call; std::chrono::steady_clock::time_point at; };
    std::vector<Evicted> retired;
    uint64_t calls{};   // ensure_runtime calls (one per evaluation)
    static constexpr uint64_t kRetireCalls = 16;
    static constexpr std::chrono::seconds kRetireTime{1};
    void free_retired();
    std::thread build_thread;
    std::mutex build_lock;
    bool building{}, build_done{}, build_linear{}, build_prep{}, build_native{};
#if NR_INT4
    // [Int4Mixed]: the network the switch asks for, the one being built, and the one running (Built::int4).
    Int4Switch int4_switch;
    int int4_want{}, build_int4{}, running_int4{-1};
#endif
    uint32_t build_passes{kMaxPasses};
    uint32_t build_w{}, build_h{};
    VkFormat build_format{VK_FORMAT_UNDEFINED};
    std::unique_ptr<Runtime> built;
    std::string build_error;
    std::chrono::steady_clock::time_point build_started{};
    double build_seconds{};
    // A build that ran out of *host* memory is a moment, not a verdict: the one
    // that failed did so 168 ms after a fullscreen transition, while DXVK was
    // re-making a 4K swapchain and the game was reloading into the same 2 GB.
    // The same build a few seconds later may well fit, so it is retried with a
    // backoff instead of latching `failed` and declining every frame for the
    // rest of the session, which is what used to happen. Every other exception
    // still latches - a device that cannot run this network will not start to.
    bool build_oom{};
    unsigned oom_failures{};
    static constexpr unsigned kOomRetries = 4;
    std::chrono::steady_clock::time_point retry_at{};
    bool linear{};
    float white_point{1.0f};
    // Model Resolution. A change rebuilds the network, like a resolution change.
    float model_scale{1.0f}, build_scale{1.0f}, built_scale{1.0f};
    bool native_compose{};   // Session::set_native_compose
    // The RenoDX-style option on a route without native compose (the ReShade add-on): its frames ask for the
    // DLL's own composition too, so the network RenoDX's processing wraps is the same on every route.
    bool renodx_native{};
    bool running_native{};   // Built::native of `runtime`
    uint32_t running_passes{};   // Built::passes of `runtime`
    bool running_prep{};         // Built::prep of `runtime`
    // Controls::preprocess was asked for once: from then on every runtime is
    // built able to run it, so the hotkey switches at once after the first
    // time. Never cleared; until then nothing about the runtime changes.
    bool prep_want{};
    // Every runtime is built able to run this many passes; Controls::passes
    // picks the count per frame.
    // What a network is built for unless the host asks for something else
    // (set_max_passes: one pass is built for one, more for at least four). The
    // ceiling is 16 and raising it costs one model-sized history image per
    // pass, which is why it is not simply set there.
    static constexpr uint32_t kMaxPasses = 4;
    static constexpr uint32_t kPassCeiling = 16;
    uint32_t max_passes_want{kMaxPasses};
    bool ensure_own_command_buffer();
    bool ensure_d3d12_device(ID3D12Device* device);
    // The Vulkan device underneath is vkd3d-proton's, which always enables bufferDeviceAddress.
    bool vkd3d_device = false;

    // ---- the bridge (nr_pe_bridge.hpp): the native D3D runtime, our own Vulkan device ----
    enum class Mode { Unknown, Interop, Bridge };
    Mode mode{Mode::Unknown};
    bridge::Device* bdev{};
    // The frame's colour (in place), motion and depth as both APIs see them. The D3D side of each is
    // ours; the game's resources are copied in and the result copied back.
    bridge::Image shared_colour, shared_motion, shared_depth;
    bridge::Image shared_exposure;   // the RenoDX-style option's exposure texture (the OptiScaler route's)
    bridge::DepthCopyD3D12 depth12;
    bridge::DepthCopyD3D11 depth11;
    bool depth_warned{};
    // Shared images replaced by a resize, freed once nothing can still be using them.
    struct RetiredImage {
        bridge::Image image;
        uint64_t after{};
        uint64_t mark{~0ull};   // the hand-over queue's position once no job of ours could use it
    };
    // Runtimes whose recorded frames were dropped (bridge::Device::on_drop, any thread): their temporal
    // state is reset before their next recording. Shared, so a drop after the session is gone is harmless.
    struct Drops {
        std::mutex m;
        std::set<const void*> runtimes;
    };
    std::shared_ptr<Drops> drops = std::make_shared<Drops>();
    // A bridge job on the current runtime, with the drop hook set and any pending drop applied.
    bridge::Job* begin_job(std::string* why);
    // The ReShade D3D12 route's queue: its own lists and their fence belong to that queue alone.
    ID3D12CommandQueue* own_queue{};
    std::vector<RetiredImage> retired_images;
    uint64_t bridge_runs{};
    bool decide_mode(bool interop_present);
    bool ensure_bridge(const LUID& luid);
    bool ensure_shared(bridge::Image& image, uint32_t w, uint32_t h, DXGI_FORMAT format, ID3D12Device* d12,
                       ID3D11Device* d11, const char* what, ID3D10Device* d10 = nullptr);
    void sweep_retired_images();
    // The network over the shared images, recorded on our device. Null (with `status`) when it could not.
    bridge::Job* record_bridge_job(const EngineFrame& base, bool motion, bool depth, const Controls& controls,
                                   const BridgeRenoDX* renodx = nullptr);
    // The RenoDX-style option's network and stage for a bridged w x h frame in `format` whose guides are
    // guide_w x guide_h. False with `status` set: the frame passes through.
    bool bridge_renodx(const Session::RenoDXFrame& rf, uint64_t feature, uint32_t w, uint32_t h, VkFormat format,
                       uint32_t guide_w, uint32_t guide_h, const Controls& controls, BridgeRenoDX* out);
    // The ReShade D3D12 route's own command lists on the game's queue, two per frame.
    struct OwnList {
        ID3D12CommandAllocator* allocator{};
        ID3D12GraphicsCommandList* list{};
        uint64_t done{};
    };
    std::vector<OwnList> own_lists;
    ID3D12Fence* own_lists_fence{};
    uint64_t own_lists_value{};
    ID3D12GraphicsCommandList* next_own_list(ID3D12Device* device);
    void release_bridge();
    void record_copy_in(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* colour,
                        D3D12_RESOURCE_STATES colour_state, ID3D12Resource* motion, D3D12_RESOURCE_STATES motion_state,
                        ID3D12Resource* depth, D3D12_RESOURCE_STATES depth_state, ID3D12Resource* exposure = nullptr,
                        D3D12_RESOURCE_STATES exposure_state = D3D12_RESOURCE_STATE_COMMON);
    // One set of shared images per extent in use (features before and after the upscale alternate).
    struct SharedSet {
        bridge::Image colour, motion, depth;
    };
    std::vector<SharedSet> parked_sets;
    void select_shared_set(uint32_t w, uint32_t h, DXGI_FORMAT fmt, const void* device);
    void record_copy_out(ID3D12GraphicsCommandList* list, ID3D12Resource* target, D3D12_RESOURCE_STATES target_state);
    bridge::Job* bridge_prepare_d3d12(ID3D12Device* device, ID3D12GraphicsCommandList* copy_list,
                                      const BridgeInputs& in, const Controls& controls,
                                      const std::function<bool()>& before_copy);
    bool bridge_d3d11(ID3D11Device* device, const D3D11Frame& frame, const Controls& controls);
    bool fits_in_memory(uint32_t w, uint32_t h, bool quiet = false);   // quiet: no log, no status
    // Session::Impl::memory_guard. False while the network is held off.
    bool memory_guard();
    struct Retired {
        Built entry;
        unsigned frames_left{};
        std::chrono::steady_clock::time_point not_before{};
    };
    std::vector<Retired> retiring;
    std::chrono::steady_clock::time_point guard_next{}, hold_until{};
    unsigned guard_trips{};
    // Make `entry` the one the run paths use. Returns false when there is no
    // such entry.
    bool select_runtime(uint32_t w, uint32_t h, VkFormat format, bool want_linear);
    // The running network serves while another builds (an [Int4Mixed] or pass-count switch): it is in use.
    void touch_running() {
        for (auto& e : cache)
            if (e.runtime.get() == runtime) { e.last = std::chrono::steady_clock::now(); e.used = ++use_stamp; }
    }
    bool have_runtime(uint32_t w, uint32_t h) const {
        for (const auto& e : cache) if (e.width == w && e.height == h) return true;
        return false;
    }
    // True when the card can hold another network at this extent. Otherwise the
    // least recently used entry is evicted (one per call) and false is returned:
    // the frame passes through and the build is asked for again later.
    bool make_room(uint32_t w, uint32_t h);
    void drop(size_t index, const char* why = "to make room");
    // Feature ids, handed out by Session::create_feature. Monotonic, never
    // reused, so a stale id can only ever miss.
    uint64_t next_feature{1};
    // One of everything per swapchain image. A binary semaphore may have exactly
    // one pending signal-wait pair, and the present waiting on ours is still
    // outstanding when the next frame is recorded - so a single one reused every
    // frame is undefined, which is what a black screen followed by a crash looks
    // like. Same argument for the command buffer and the fence.
    struct PresentSlot {
        VkCommandBuffer cmd{};
        VkFence fence{};
        VkSemaphore done{};
        bool pending{};
    };
    std::vector<PresentSlot> slots;
    VkCommandPool slot_pool{};
    uint32_t slot_family{~0u};
    bool ensure_slots(uint32_t count);
    bool wait_slot(PresentSlot& slot);
    void release_slots();
    // Resetting a command buffer the GPU is still reading is undefined, and at
    // a 4K working extent the GPU is a long way behind by the time the next
    // frame arrives. One fence, waited on before each reuse.
    VkFence own_fence{};
    uint32_t pool_family{~0u};
    // An extent seen but not yet built for, and how many consecutive frames it
    // has held. See run_present_vulkan.
    uint32_t pending_w{}, pending_h{}, pending_frames{};
    bool own_cmd_pending{};
    bool wait_for_own_cmd();
};

void Session::Impl::release_vk_output() {
    if (vk_output_view) { vkDestroyImageView(handles.device, vk_output_view, nullptr); vk_output_view = VK_NULL_HANDLE; }
    if (vk_output) { vkDestroyImage(handles.device, vk_output, nullptr); vk_output = VK_NULL_HANDLE; }
    if (vk_output_memory) { vkFreeMemory(handles.device, vk_output_memory, nullptr); vk_output_memory = VK_NULL_HANDLE; }
    vk_w = vk_h = 0; vk_format = VK_FORMAT_UNDEFINED;
}

bool Session::Impl::ensure_vk_output(uint32_t w, uint32_t h, VkFormat format) {
    release_vk_output();
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D; ci.format = format; ci.extent = {w, h, 1};
    ci.mipLevels = ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(handles.device, &ci, nullptr, &vk_output) != VK_SUCCESS) {
        status = "could not create the render-resolution output image";
        return false;
    }
    VkMemoryRequirements req{}; vkGetImageMemoryRequirements(handles.device, vk_output, &req);
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceMemoryProperties(handles.physical, &mem);
    uint32_t type = mem.memoryTypeCount;
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) { type = i; break; }
    if (type == mem.memoryTypeCount) { release_vk_output(); status = "no device-local memory"; return false; }
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size; alloc.memoryTypeIndex = type;
    if (vkAllocateMemory(handles.device, &alloc, nullptr, &vk_output_memory) != VK_SUCCESS ||
        vkBindImageMemory(handles.device, vk_output, vk_output_memory, 0) != VK_SUCCESS) {
        release_vk_output(); status = "could not back the output image"; return false;
    }
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = vk_output; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(handles.device, &vi, nullptr, &vk_output_view) != VK_SUCCESS) {
        release_vk_output(); status = "could not view the output image"; return false;
    }
    vk_w = w; vk_h = h; vk_format = format;
    return true;
}

bool Session::Impl::wait_for_own_cmd() {
    if (!own_cmd_pending || !own_fence) return true;
    // A second is far longer than any frame; reaching it means the device is
    // lost or hung, and continuing to submit into that is how a hang becomes a
    // crash.
    const VkResult r = vkWaitForFences(handles.device, 1, &own_fence, VK_TRUE, 1000000000ull);
    if (r != VK_SUCCESS) {
        status = "the previous neural rendering submit never completed";
        failed = true;
        return false;
    }
    vkResetFences(handles.device, 1, &own_fence);
    own_cmd_pending = false;
    return true;
}

bool Session::Impl::ensure_own_command_buffer() {
    // A pool belongs to one queue family for its whole life. If the family we
    // submit on has changed - and it does, because the queue a game presents on
    // is not necessarily the one we found when looking for a compute queue -
    // the pool has to go with it. Submitting a buffer from another family's pool
    // is invalid and takes the process down.
    if (own_cmd && pool_family != access.family) {
        wait_for_own_cmd();
        if (pool) { vkDestroyCommandPool(handles.device, pool, nullptr); pool = VK_NULL_HANDLE; }
        own_cmd = VK_NULL_HANDLE;
    }
    if (own_cmd) return true;
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex = access.family;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(handles.device, &pi, nullptr, &pool) != VK_SUCCESS) {
        status = "could not create a command pool on the game's device";
        return false;
    }
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(handles.device, &ai, &own_cmd) != VK_SUCCESS) {
        status = "could not allocate a command buffer";
        return false;
    }
    pool_family = access.family;
    if (own_fence) return true;
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(handles.device, &fi, nullptr, &own_fence) != VK_SUCCESS) {
        status = "could not create the fence that paces our own submits";
        return false;
    }
    return true;
}

bool Session::Impl::wait_slot(PresentSlot& slot) {
    if (!slot.pending) return true;
    const VkResult r = vkWaitForFences(handles.device, 1, &slot.fence, VK_TRUE, 1000000000ull);
    if (r != VK_SUCCESS) {
        status = "the previous neural rendering submit never completed";
        failed = true;
        return false;
    }
    vkResetFences(handles.device, 1, &slot.fence);
    slot.pending = false;
    return true;
}

void Session::Impl::release_slots() {
    for (auto& slot : slots) {
        wait_slot(slot);
        if (slot.done) vkDestroySemaphore(handles.device, slot.done, nullptr);
        if (slot.fence) vkDestroyFence(handles.device, slot.fence, nullptr);
    }
    slots.clear();
    if (slot_pool) { vkDestroyCommandPool(handles.device, slot_pool, nullptr); slot_pool = VK_NULL_HANDLE; }
    slot_family = ~0u;
}

bool Session::Impl::ensure_slots(uint32_t count) {
    if (count == 0) count = 1;
    if (slots.size() == count && slot_family == access.family) return true;
    release_slots();

    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex = access.family;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(handles.device, &pi, nullptr, &slot_pool) != VK_SUCCESS) {
        status = "could not create a command pool on the game's present family";
        return false;
    }
    std::vector<VkCommandBuffer> buffers(count);
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = slot_pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = count;
    if (vkAllocateCommandBuffers(handles.device, &ai, buffers.data()) != VK_SUCCESS) {
        status = "could not allocate the per-image command buffers";
        return false;
    }
    slots.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        slots[i].cmd = buffers[i];
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (vkCreateFence(handles.device, &fi, nullptr, &slots[i].fence) != VK_SUCCESS ||
            vkCreateSemaphore(handles.device, &si, nullptr, &slots[i].done) != VK_SUCCESS) {
            status = "could not create the per-image fence and semaphore";
            release_slots();
            return false;
        }
    }
    slot_family = access.family;
    log("[nr] %u present slots on queue family %u", count, access.family);
    return true;
}

// Will the network fit in what is left of the card?
//
// The activation arena scales with the working extent and it is not small: 1.05
// GB at 1920x1088 and 4.07 GB at 3840x2176 with every value in its own bytes,
// about 500 bytes per pixel; 384 MB and 1.53 GB since values share memory by
// lifetime (nr_graph.cpp `reuse`), about 185 bytes per pixel (200 below, for
// the small extents' fixed part). NR_ARENA_REUSE=0 turns sharing off. A game at 4K has already taken most of a 16 GB card, so the
// fallback - which runs at output resolution, because it works on the finished
// frame - asks for four gigabytes that are not there. Allocation then fails
// somewhere inside the graph build, or succeeds by evicting the game's own
// resources, and what the player sees is a black frame and a crash.
//
// Refusing with a number in the log is strictly better than either.
static uint64_t arena_bytes_per_pixel() {
    const char* e = std::getenv("NR_ARENA_REUSE");
    return e && *e && std::atoi(e) == 0 ? 520 : 200;
}

bool Session::Impl::fits_in_memory(uint32_t w, uint32_t h, bool quiet) {
    // The arena is sized from the padded model extent (Model Resolution), the
    // frame-sized images from the frame; plus weights and scratch.
    const auto model = [&](uint32_t v) {
        return model_scale >= 1.0f ? v : std::max(8u, uint32_t(double(v) * model_scale + 0.5));
    };
    const uint64_t pixels = uint64_t((model(w) + 63) / 64 * 64) * ((model(h) + 63) / 64 * 64);
    const uint64_t need = pixels * arena_bytes_per_pixel() + uint64_t(w) * h * 48 + (300ull << 20);

    const MemoryState m = read_memory(handles.physical);
    const char* short_of = nullptr;
    uint64_t have = 0, reserve = 0;
    if (m.vram_known && need + kVramReserve > m.vram_free) {
        short_of = "video memory"; have = m.vram_free; reserve = kVramReserve;
    } else if (m.commit_limit && need + kCommitReserve > m.commit_free) {
        short_of = "system memory (RAM + page file)"; have = m.commit_free; reserve = kCommitReserve;
    }
    if (quiet) return !short_of;
    if (short_of) {
        char message[320];
        std::snprintf(message, sizeof message,
                      "the network needs about %.0f MB at %ux%u (model scale %.2f) and %.0f MB of %s is "
                      "free, %.0f MB of which stays with the game; not building it. A lower Model "
                      "Resolution, a larger page file or closing other programs makes room.",
                      mb(need), w, h, model_scale, mb(have), short_of, mb(reserve));
        status = message;
        log("[nr] %s", message);
        return false;
    }
    log("[nr] %ux%u (model scale %.2f) needs about %.0f MB; free: VRAM %.0f MB, commit %.0f MB", w, h,
        model_scale, mb(need), mb(m.vram_free), mb(m.commit_free));
    return true;
}

// **The running network steps aside when the machine runs short.** Checked at
// most twice a second. The game grows after the build was admitted - loading a
// zone, a resize, another program - and at the limit it is the game that gets
// the failed allocation. So below kVramLow / kCommitLow every resident network
// is retired: taken out of use at once (frames pass through) and destroyed a
// few frames later, when nothing the GPU still has in flight can reference it
// (on the paths that record into the game's command list there is no fence of
// ours to wait on). A rebuild then has to pass fits_in_memory with its reserves,
// and not before a hold that doubles each time, so a machine that stays short
// is not rebuilt into again and again.
bool Session::Impl::memory_guard() {
    const auto now = std::chrono::steady_clock::now();
    // Retired networks, destroyed once they are surely idle.
    for (auto it = retiring.begin(); it != retiring.end();) {
        if (it->frames_left) --it->frames_left;
        // On the bridge, not while a recorded or running frame of ours still uses it.
        if (!it->frames_left && now >= it->not_before && !(bdev && bdev->busy(it->entry.runtime.get()))) {
            wait_for_own_cmd();
            it = retiring.erase(it);
        } else {
            ++it;
        }
    }
    if (now < guard_next) return now >= hold_until;
    guard_next = now + std::chrono::milliseconds(500);
    if (cache.empty()) return now >= hold_until;
    const MemoryState m = read_memory(handles.physical);
    const bool vram_low = m.vram_known && m.vram_free < kVramLow;
    const bool commit_low = m.commit_limit && m.commit_free < kCommitLow;
    if (!vram_low && !commit_low) return now >= hold_until;

    const unsigned hold = std::min(10u << std::min(guard_trips, 4u), 160u);
    ++guard_trips;
    log("[nr] memory is running out (VRAM %.0f MB free of %.0f, commit %.0f MB free of %.0f): "
        "the network steps aside for %u s; %u network(s) released",
        mb(m.vram_free), mb(m.vram_budget), mb(m.commit_free), mb(m.commit_limit), hold,
        unsigned(cache.size()));
    for (auto& e : cache) {
        Retired r;
        r.entry = std::move(e);
        r.frames_left = 16;
        r.not_before = now + std::chrono::seconds(2);
        retiring.push_back(std::move(r));
    }
    cache.clear();
    runtime = nullptr; width = height = 0;
    hold_until = now + std::chrono::seconds(hold);
    return false;
}

bool Session::Impl::select_runtime(uint32_t w, uint32_t h, VkFormat format, bool want_linear) {
    for (auto& e : cache) {
        if (e.width != w || e.height != h || e.format != format || e.linear != want_linear ||
            e.scale != model_scale || e.passes != max_passes_want || e.prep != prep_want ||
            e.native != (native_compose || renodx_native))
            continue;
#if NR_INT4
        if (e.int4 != int4_want) continue;   // [Int4Mixed]: the network the switch asks for
#endif
        e.used = ++use_stamp;
        const auto now = std::chrono::steady_clock::now();
        if (runtime != e.runtime.get()) {
            const bool stale = e.last != std::chrono::steady_clock::time_point{} && now - e.last > kStaleHistory;
            if (stale) e.runtime->forget_history();
            log("[nr] runtime cache hit: %ux%u (model %ux%u, scale %.2f%s), %u built, "
                "%u live features%s", e.width, e.height, e.runtime->model_width(),
                e.runtime->model_height(), e.scale, e.linear ? ", linear" : "",
                unsigned(cache.size()), e.runtime->live_features(), stale ? ", history started afresh" : "");
        }
        e.last = now;
        runtime = e.runtime.get();
        width = e.width; height = e.height; this->format = e.format;
        linear = e.linear; built_scale = e.scale; running_native = e.native;
        running_passes = e.passes; running_prep = e.prep;
#if NR_INT4
        running_int4 = e.int4;
#endif
        return true;
    }
    return false;
}

void Session::Impl::drop(size_t index, const char* why) {
    auto& e = cache[index];
    log("[nr] evicting the %ux%u network %s", e.width, e.height, why);
    if (runtime == e.runtime.get()) { runtime = nullptr; width = height = 0; }
    // Nothing of ours may still be reading it. On the paths that submit for
    // themselves this waits on our own fence; on the paths that record into the
    // game's command list the game's frames in flight may still hold it, so it
    // is not destroyed here but retired (see Retired).
    wait_for_own_cmd();
    retired.push_back({std::move(e.runtime), calls, std::chrono::steady_clock::now()});
    cache.erase(cache.begin() + long(index));
}

void Session::Impl::free_retired() {
    const auto now = std::chrono::steady_clock::now();
    for (size_t i = retired.size(); i-- > 0;) {
        if (calls - retired[i].call < kRetireCalls || now - retired[i].at < kRetireTime) continue;
        // On the bridge a recorded frame runs when its command list does, which the game decides: not
        // while one that uses this network is recorded and not yet run, or still running.
        if (bdev && bdev->busy(retired[i].runtime.get())) continue;
        retired.erase(retired.begin() + long(i));
        log("[nr] evicted network freed");
    }
}

bool Session::Impl::make_room(uint32_t w, uint32_t h) {
    if (fits_in_memory(w, h)) return true;
    if (cache.empty()) return false;
    size_t oldest = 0;
    for (size_t i = 1; i < cache.size(); ++i)
        if (cache[i].used < cache[oldest].used) oldest = i;
    // One at a time: an evicted network keeps its memory until it is freed (see Retired), and the build is asked
    // for again once it has been.
    drop(oldest);
    status = "freeing the least recently used network to make room";
    return false;
}


bool Session::Impl::ensure_runtime(const Controls& controls, uint32_t w, uint32_t h, VkFormat format,
                                   bool want_linear) {
    const bool ok = ensure_runtime_(controls, w, h, format, want_linear);
    if (ok && controls.preprocess.active() && controls.preprocess.exposure == 1) {
        const auto now = std::chrono::steady_clock::now();
        const auto m = runtime->preprocess_meter();
        if (now - meter_logged > std::chrono::seconds(5) && m.first == m.first) {
            meter_logged = now;
            const float bias = controls.preprocess.bias_ev;
            log("[nr] preprocess exposure: auto %+.2f EV (this frame asks %+.2f) + ExposureBias %+.2f = %+.2f EV",
                m.first, m.second, bias, m.first + bias);
        }
    }
    return ok;
}

bool Session::Impl::ensure_runtime_(const Controls& controls, uint32_t w, uint32_t h, VkFormat format,
                                    bool want_linear) {
    ++calls;
    if (!retired.empty()) free_retired();
    if (controls.preprocess.active() && !prep_want) {
        prep_want = true;
        log("[nr] preprocess asked for: networks are rebuilt able to run it (once)");
    }
#if NR_INT4
    int4_want = int4_switch.frame(root + "\\dlssnr-amd.ini");
#endif
    // Adopt a finished background build first.
    {
        std::lock_guard<std::mutex> guard(build_lock);
        if (build_done) {
            if (build_thread.joinable()) build_thread.join();
            build_done = false; building = false;
            if (built) {
                Built entry;
                entry.width = build_w; entry.height = build_h; entry.format = build_format;
                entry.linear = build_linear; entry.scale = build_scale;
                entry.passes = build_passes; entry.prep = build_prep; entry.native = build_native;
#if NR_INT4
                // what was built, not what was asked: an int4 build without what it needs is the default network
                entry.int4 = built->int4_mixed() ? 1 : 0;
                if (build_int4 && !entry.int4) { int4_switch.unavailable(); int4_want = 0; }   // not asked again this run
#endif
                entry.used = ++use_stamp;
                entry.last = std::chrono::steady_clock::now();
                entry.runtime = std::move(built);
                // The pass count went back while this one built, and the running network is the one wanted again:
                // this one is not wanted; the running one keeps serving.
                const bool unwanted = entry.passes != max_passes_want && runtime && running_passes == max_passes_want &&
                                      width == entry.width && height == entry.height && this->format == entry.format;
                if (unwanted) {
                    log("[nr] the network built for %u passes is not wanted any more (the pass count changed back)",
                        entry.passes);
                    retired.push_back({std::move(entry.runtime), calls, std::chrono::steady_clock::now()});
                } else {
                log("[nr] network built at %ux%u (model %ux%u, scale %.2f) on queue family %u in "
                    "%.1f s%s; %u networks resident",
                    entry.width, entry.height, entry.runtime->model_width(),
                    entry.runtime->model_height(), entry.scale, access.family, build_seconds,
                    entry.linear ? "; the colour is linear light and is encoded for the network" : "",
                    unsigned(cache.size() + 1));
                // A network built without the preprocess at this extent is
                // never selected again; its memory goes now. Its last frame
                // was before this build started, a second or more ago.
                for (size_t i = cache.size(); i-- > 0;)
                    if (entry.prep && !cache[i].prep && cache[i].width == entry.width &&
                        cache[i].height == entry.height)
                        drop(i, "built without the preprocess");
#if NR_INT4
                // [Int4Mixed] switch: unless KeepBoth, only one kind of network stays resident. The one the switch left
                // keeps no memory once its replacement is in (switching back builds it again, the running one serving).
                if (!int4_switch.keep_both())
                    for (size_t i = cache.size(); i-- > 0;)
                        if (cache[i].int4 != entry.int4) drop(i, entry.int4 ? "(default; int4 mixed is in)" : "(int4 mixed; default is in)");
#endif
                // A change of pass count: the network built for the other count served until now and keeps no
                // memory once this one is in.
                for (size_t i = cache.size(); i-- > 0;)
                    if (cache[i].passes != entry.passes && cache[i].width == entry.width &&
                        cache[i].height == entry.height && cache[i].format == entry.format &&
                        cache[i].linear == entry.linear && cache[i].scale == entry.scale &&
                        cache[i].prep == entry.prep && cache[i].native == entry.native)
                        drop(i, "(built for another pass count)");
                cache.push_back(std::move(entry));
                auto& adopted = cache.back();
                runtime = adopted.runtime.get();
                width = adopted.width; height = adopted.height; this->format = adopted.format;
                linear = adopted.linear; built_scale = adopted.scale; running_native = adopted.native;
                running_passes = adopted.passes; running_prep = adopted.prep;
#if NR_INT4
                running_int4 = adopted.int4;
#endif
                }
                oom_failures = 0;
                retry_at = {};
            } else {
                status = build_error;
                log("[nr] %s", status.c_str());
                if (build_oom && oom_failures < kOomRetries) {
                    // 4, 8, 16, 32 seconds. Long enough that the load or the
                    // resolution change that took the address space has
                    // finished, short enough that a player does not give up on
                    // the feature first.
                    const unsigned wait = 4u << oom_failures;
                    ++oom_failures;
                    retry_at = std::chrono::steady_clock::now() + std::chrono::seconds(wait);
                    log("[nr] retrying the build in %u s (attempt %u of %u)", wait, oom_failures,
                        kOomRetries);
                } else {
                    failed = true;
                }
                return false;
            }
        }
    }
    if (!memory_guard()) {
        status = "memory is short; the network is held off and frames pass through";
        return false;
    }
    if (select_runtime(w, h, format, want_linear)) return true;
#if NR_INT4
    // [Int4Mixed] switch: the running network keeps serving its frames while the other one builds, so the
    // comparison has no gap but the build itself (the first int4 build also compiles its pipelines).
    const bool int4_only = runtime && running_int4 >= 0 && running_int4 != int4_want && width == w && height == h &&
                           this->format == format && linear == want_linear && built_scale == model_scale &&
                           running_native == (native_compose || renodx_native);
#endif
    // A change of pass count (set_max_passes): the running network serves, at most at the passes it was built for,
    // while the one for the new count builds.
    static constexpr const char* kPassesServing =
        "building the network for the new pass count in the background; the running one serves until it is ready";
    const bool passes_only = runtime && running_passes != max_passes_want && width == w && height == h &&
                             this->format == format && linear == want_linear && built_scale == model_scale &&
                             running_native == (native_compose || renodx_native) && running_prep == prep_want
#if NR_INT4
                             && running_int4 == int4_want
#endif
        ;
    if (building) {
#if NR_INT4
        if (int4_only) { touch_running(); return true; }
#endif
        if (passes_only) { touch_running(); status = kPassesServing; return true; }
        status = "building the network in the background; frames pass through until it is ready";
        return false;
    }
    if (!access.valid()) {
        status = "no submittable queue on the game's device; the weights cannot be uploaded";
        failed = true;
        return false;
    }
    // A host-memory failure is waiting out its backoff; the frame passes through
    // with the reason still in `status`, exactly as it does while a build runs.
    if (retry_at != std::chrono::steady_clock::time_point{}) {
        if (std::chrono::steady_clock::now() < retry_at) {
            status = build_error;
            return false;
        }
        retry_at = {};
    }
    // An evicted network still holds its memory until it is freed. If the next one does not fit beside it, wait
    // for that rather than push out another: the switch keeps the running network serving, anything else passes
    // through.
    if (!retired.empty() && !fits_in_memory(w, h, true)) {
#if NR_INT4
        if (int4_only) { touch_running(); return true; }
#endif
        if (passes_only) { touch_running(); status = kPassesServing; return true; }
        status = "freeing the previous network before building another";
        return false;
    }
    // Checked before anything is allocated, and not treated as a hard failure:
    // a smaller extent may well fit later, and a game changing resolution is
    // exactly when that happens. Cached networks at other extents are given up
    // one at a time, oldest first, until this one fits.
    if (!make_room(w, h)) return false;
    RuntimeConfig config;
    config.root = root; config.width = w; config.height = h; config.colour_format = format;
    config.linear_input = want_linear; config.white_point = white_point;
    config.model_scale = model_scale;
    const bool native = native_compose || renodx_native;
    config.max_passes = native ? 1u : max_passes_want;
    config.native_compose = native;
    config.preprocess = prep_want;
#if NR_INT4
    config.int4_mixed = int4_want;
#endif
    // The soft knee to undo: the linear path's own encode, or OptiScaler's
    // linear-HDR encode, which a float proxy on the OptiScaler route came
    // through (OptiScaler encodes a float colour flagged IsHDR or AutoExposure
    // and hands an SDR one over as it is, and only the format reaches us).
    config.preprocess_unknee = want_linear || (native && is_linear_format(format));
    if (native) config.model_scale = 1.0f;   // the DLL has no resample; OptiScaler scales outside
    TemporalConfig temporal; temporal.enable = true;
    temporal.history_strength = history_strength;
    HostDevice host{};
    host.instance = handles.instance; host.physical = handles.physical; host.device = handles.device;
    // A real queue and its real family. Construction submits the weight upload
    // and waits for it; the game is submitting to the same underlying VkQueue,
    // so each of those submits takes the queue lock (below).
    host.queue = access.queue; host.queue_family = access.family;
    host.buffer_device_address = vkd3d_device || mode == Mode::Bridge ||
                                 vkdevice::network_features_added(handles.device);
    // Off the render thread. Building reads the weights, creates every pipeline
    // and allocates the arena, and doing that inside the upscaler's call froze
    // the game for the duration every time the feature was first turned on. The
    // queue lock is a mutex and is taken from this thread exactly as it would be
    // from the render thread; the game keeps rendering unenhanced frames until
    // the build is adopted above.
    const QueueAccess access_copy = access;
    build_started = std::chrono::steady_clock::now();
    nr::g_build_stage = 1; nr::g_build_pipes_done = 0; nr::g_build_pipes_total = 0;
    building = true; build_w = w; build_h = h; build_format = format; build_linear = want_linear;
    build_scale = model_scale; build_passes = max_passes_want; build_prep = prep_want; build_native = native;
#if NR_INT4
    build_int4 = int4_want;
#endif
    status = "building the network in the background; frames pass through until it is ready";
#if NR_INT4
    log("[nr] network asked for: %s", int4_want ? "int4 mixed" : "default");
#endif
    VkPhysicalDeviceProperties gpu{};
    vkGetPhysicalDeviceProperties(handles.physical, &gpu);
    log("[nr] building the network at %ux%u (model scale %.2f) on %s in the background%s%s", w, h, model_scale,
        gpu.deviceName,
        want_linear ? " (linear-light colour: encoding with a white point, see dlssnr-amd.ini white_point)" : "",
        nr::binary::directory().empty() ? "" : ", ACO's machine code ([Network] ACO Mode)");
    {
        static std::string said;
        const std::string why = nr::binary::disabled_why();
        if (!why.empty() && why != said) { said = why; log("[nr] [Network] ACO Mode is off for this run: %s", why.c_str()); }
    }
    build_thread = std::thread([this, host, config, temporal, access_copy] {
        const auto t0 = std::chrono::steady_clock::now();
        std::unique_ptr<Runtime> made;
        std::string error;
        bool oom = false;
        // The queue lock around each submit of the build, not around the
        // build: held for the whole of it, the pipeline compile (14-24 s
        // cold at 4K under AMD's Windows compiler) blocked vkd3d's own
        // submissions and presents on the shared queue, and the game froze
        // for as long. **Quiet**: this is not the render thread, and the
        // flush half of the other lock drives DXVK's immediate context,
        // which that thread owns. See QueueAccess::lock_quiet.
        HostDevice locked = host;
        locked.queue_lock = access_copy.lock_quiet;
        locked.queue_unlock = access_copy.unlock_quiet;
        try {
            try {
                made = std::make_unique<Runtime>(locked, config, ControlMaskConfig{}, temporal);
            } catch (const std::bad_alloc&) {
                throw;
            } catch (const std::exception& e) {
#if NR_INT4
                if (config.int4_mixed == 1) {
                    log("[nr] int4 mixed network failed: %s; building the default network", e.what());
                    RuntimeConfig fallback = config;
                    fallback.int4_mixed = 0;
                    made = std::make_unique<Runtime>(locked, fallback, ControlMaskConfig{}, temporal);
                } else
#endif
                {
                    if (nr::binary::directory().empty()) throw;
                    nr::binary::disable(std::string("the ACO network did not build (") + e.what() + ")");
                    log("[nr] [Network] ACO Mode: %s; building with the driver's compiler", nr::binary::disabled_why().c_str());
                    made = std::make_unique<Runtime>(locked, config, ControlMaskConfig{}, temporal);
                }
            }
        } catch (const std::bad_alloc& e) {
            // Caught apart from everything else because it is the one failure
            // that is about the *host* and is worth retrying: operator new, or
            // Vulkan's own host-memory / mapping failures (nrvk::HostMemoryError).
            // For a plain bad_alloc `what()` is the useless "std::bad_alloc";
            // what a reader needs is how much address space was left, and the
            // build's own log lines above say what it had just asked for.
            oom = true;
            error = std::string("neural rendering unavailable: out of host memory (") + e.what() + "). " +
                    address_space_report();
        } catch (const std::exception& e) {
            error = std::string("neural rendering unavailable: ") + e.what();
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::lock_guard<std::mutex> guard(build_lock);
        built = std::move(made); build_error = error; build_seconds = seconds; build_done = true;
        build_oom = oom;
    });
#if NR_INT4
    if (int4_only && runtime != nullptr) { touch_running(); return true; }   // an [Int4Mixed] switch: the running one serves until the new one is in
#endif
    if (passes_only && runtime != nullptr) {   // a change of pass count, the same
        touch_running();
        status = kPassesServing;
        return true;
    }
    return false;
}

void Session::Impl::release_output() {
    if (output) { output->Release(); output = nullptr; }
    output_vk = ResourceHandle{};
}

void Session::Impl::release_invalidator() {
    if (invalidator_gpu_heap) { invalidator_gpu_heap->Release(); invalidator_gpu_heap = nullptr; }
    if (invalidator_cpu_heap) { invalidator_cpu_heap->Release(); invalidator_cpu_heap = nullptr; }
    if (invalidator) { invalidator->Release(); invalidator = nullptr; }
}

// A 2x1 R32_UINT texture and the two descriptors a ClearUAV needs. Two texels
// because the clear below asks for ONE of them: see forget_bound_pipeline.
bool Session::Impl::ensure_invalidator(ID3D12Device* device) {
    if (invalidator && invalidator_cpu_heap && invalidator_gpu_heap) return true;
    if (invalidator_failed || !device) return false;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 2;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_UINT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                 IID_PPV_ARGS(&invalidator));
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = 1;
    if (SUCCEEDED(hr))
        hr = device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&invalidator_cpu_heap));
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (SUCCEEDED(hr))
        hr = device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&invalidator_gpu_heap));
    if (FAILED(hr)) {
        release_invalidator();
        invalidator_failed = true;
        log("[nr] could not build the pipeline invalidator (0x%08lX); a second neural pass in one "
            "command list will leave the caller's next dispatch bound to our kernel",
            (unsigned long)hr);
        return false;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(invalidator, nullptr, &uav,
                                      invalidator_cpu_heap->GetCPUDescriptorHandleForHeapStart());
    device->CreateUnorderedAccessView(invalidator, nullptr, &uav,
                                      invalidator_gpu_heap->GetCPUDescriptorHandleForHeapStart());
    return true;
}

// **Why a ClearUAV of two texels is the fix for multipass.** Measured with
// a D3D12 test harness, which drives our _nvngx.dll the way
// OptiScaler does, with OptiScaler's own codec shader between and after the
// evaluates, and reads every surface back:
//
//   one pass  - the caller's dispatch after the evaluate is correct.
//   two passes- the clamp (after the FIRST evaluate) is correct; every dispatch
//               after the SECOND evaluate runs our last network kernel instead
//               of the caller's shader. Its target is never written, so the
//               frame keeps whatever it held: the unedited picture. That is the
//               NR-off picture, exactly as reported in the game.
//
// The cause is in vkd3d-proton (libs/vkd3d/command.c). It tracks TWO pipelines:
// `state`/`current_pipeline`, the D3D12-level one, and `command_buffer_pipeline`,
// what it believes is really bound in the VkCommandBuffer. Our
// SetPipelineState(nullptr) below clears the first, so the caller's next
// SetPipelineState is not deduplicated at the D3D12 level - but the bind itself
// is still skipped, because the recomputed VkPipeline equals
// `command_buffer_pipeline`. The first dispatch after a recording escapes only
// because that caller pipeline had never been bound in this command buffer yet.
//
// vkd3d has exactly one mechanism for "something bound its own pipeline behind
// my back": d3d12_command_list_invalidate_current_pipeline(list, meta_shader=true),
// which sets `command_buffer_pipeline = VK_NULL_HANDLE` - "just pretend we never
// bound anything". It is not on any interface; it runs when vkd3d dispatches one
// of its own meta shaders. ClearUnorderedAccessViewUint takes that path whenever
// its fast path cannot serve the clear, and a rect covering PART of the image is
// refused by the fast path (d3d12_command_list_clear_uav_builtin: "Accept a
// single full-subresource rect"). So: two texels, clear one.
//
// It costs one tiny meta dispatch per evaluate and touches no state the caller
// can observe - unlike ClearState, which would do the same job by also throwing
// away the caller's render targets and bindings.
void Session::Impl::forget_bound_pipeline(ID3D12Device* device, ID3D12GraphicsCommandList* list) {
    if (!ensure_invalidator(device)) return;
    const UINT zero[4] = {0, 0, 0, 0};
    const D3D12_RECT rect{0, 0, 1, 1};   // half of a 2x1 image: the fast path must refuse it
    list->ClearUnorderedAccessViewUint(invalidator_gpu_heap->GetGPUDescriptorHandleForHeapStart(),
                                       invalidator_cpu_heap->GetCPUDescriptorHandleForHeapStart(),
                                       invalidator, zero, 1, &rect);
}

bool Session::Impl::ensure_output(ID3D12Device* device, uint32_t w, uint32_t h, DXGI_FORMAT dxgi) {
    if (output) {
        const auto desc = output->GetDesc();
        if (desc.Width == w && desc.Height == h && desc.Format == dxgi) return true;
        release_output();
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = w; desc.Height = h; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.Format = dxgi; desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                               __uuidof(ID3D12Resource),
                                               reinterpret_cast<void**>(&output)))) {
        status = "could not allocate the render-resolution output";
        return false;
    }
    output_vk = colour_handle(device, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!output_vk.usable()) {
        release_output();
        status = "the output has no Vulkan image; this is not vkd3d-proton";
        return false;
    }
    return true;
}

namespace {

// One place turns a D3D12 resource into the ColourFrame the runtime wants, so
// colour, motion and depth cannot drift apart in layout handling.
bool fill(ColourFrame* out, const ResourceHandle& handle) {
    if (!handle.usable()) return false;
    out->image = handle.image;
    out->format = handle.format;
    out->width = handle.width; out->height = handle.height;
    out->before = out->after = handle.layout;
    out->usage = handle.usage;
    return true;
}

// Narrow a guide to the part of it the game rendered into: its subrect's base and extent, which the
// runtime reads from (EngineFrame::motion_x/depth_x and the narrowed width/height) - its blits take
// the region as their source rectangle, the in-place motion sample maps uv into it. A base outside
// the texture keeps the whole allocation (logged once).
void apply_guide_subrect(ColourFrame* guide, const Session::Subrect& want, const char* what,
                         uint32_t* base_x, uint32_t* base_y) {
    if (!guide->image) return;
    const uint32_t aw = guide->width, ah = guide->height;
    const int which = what[0] == 'd' ? 0 : 1;
    if (want.x >= aw || want.y >= ah) {
        static bool outside[2] = {false, false};
        if (!outside[which]) {
            outside[which] = true;
            log("[nr] the %s guide's subrect starts at (%u,%u), outside its %ux%u texture; the whole texture is used",
                what, want.x, want.y, aw, ah);
        }
        return;
    }
    // The region the game rendered into: from the base, the given extent (or the
    // rest of the texture), clipped to the texture - NVIDIA's own reading of the
    // subrect (base and extent, nvngx_dlssnr.dll 0x180022278).
    const uint32_t w = want.width ? std::min(want.width, aw - want.x) : aw - want.x;
    const uint32_t h = want.height ? std::min(want.height, ah - want.y) : ah - want.y;
    *base_x = want.x; *base_y = want.y;
    // A depth region that is not the whole image is read through the runtime's
    // copy, not sampled in place. Motion keeps its usage: it is sampled in place
    // through the region's base and share of the allocation.
    if (which == 0 && (w < aw || h < ah)) guide->usage = 0;
    guide->width = w; guide->height = h;
    if (want.x || want.y) {
        static bool noted[2] = {false, false};
        if (!noted[which]) {
            noted[which] = true;
            log("[nr] the %s guide's region starts at (%u,%u): %ux%u of the %ux%u texture", what, want.x, want.y,
                w, h, aw, ah);
        }
    }
}

}  // namespace

bool Session::Impl::ensure_d3d12_device(ID3D12Device* device) {
    if (handles.valid() && access.valid()) return true;
    if (!handles.valid()) {
        handles = device_handles(device);
        vkd3d_device = handles.valid();
        if (!handles.valid()) {
            status = "no Vulkan device underneath; not running on vkd3d-proton";
            failed = true;
            log("[nr] %s", status.c_str());
            return false;
        }
    }
    if (!access.valid() && !d3d12_queue_access(device, &access, &owned_queue)) {
        status = "vkd3d-proton would not hand out a queue; the weights cannot be uploaded";
        failed = true;
        log("[nr] %s", status.c_str());
        return false;
    }
    return true;
}

// ---- the bridge -------------------------------------------------------------------------------------

bool Session::Impl::decide_mode(bool interop_present) {
    if (mode != Mode::Unknown) return mode == Mode::Bridge;
    static const bool forced = [] {
        char v[4] = {};
        const DWORD n = GetEnvironmentVariableA("NR_BRIDGE", v, sizeof v);
        return n > 0 && n < sizeof v && v[0] == '1';
    }();
    mode = interop_present && !forced ? Mode::Interop : Mode::Bridge;
    if (mode == Mode::Bridge)
        log("[nr] %s: the network runs on a Vulkan device of its own beside the game's D3D device",
            interop_present ? "NR_BRIDGE=1" : "native D3D runtime");
    return mode == Mode::Bridge;
}

bool Session::Impl::ensure_bridge(const LUID& luid) {
    if (bdev) return true;
    std::string why;
    bdev = bridge::Device::get(luid, &why);
    if (!bdev) {
        status = "neural rendering unavailable: " + why;
        failed = true;
        return false;
    }
    handles = bdev->handles();
    access.queue = bdev->queue();
    access.family = bdev->family();
    // Our queue is ours: the only other submitter is the build thread's weight upload.
    bridge::Device* d = bdev;
    access.lock = access.lock_quiet = [d] { d->lock(); };
    access.unlock = access.unlock_quiet = [d] { d->unlock(); };
    return true;
}

bool Session::Impl::ensure_shared(bridge::Image& image, uint32_t w, uint32_t h, DXGI_FORMAT format,
                                  ID3D12Device* d12, ID3D11Device* d11, const char* what, ID3D10Device* d10) {
    const void* device = d12 ? static_cast<const void*>(d12) : d11 ? static_cast<const void*>(d11) : d10;
    if (image.matches(w, h, format, device)) return true;
    if (image) {
        retired_images.push_back({image, bridge_runs + 16});
        image = bridge::Image{};
    }
    std::string why;
    const bool ok = d12 ? bdev->create_d3d12(d12, w, h, format, &image, &why)
                  : d11 ? bdev->create_d3d11(d11, w, h, format, &image, &why)
                        : bdev->create_d3d10(d10, w, h, format, &image, &why);
    if (!ok) {
        status = std::string("the shared ") + what + " image: " + why;
        thread_local std::string said;
        if (said != status) { said = status; log("[nr] bridge: %s", status.c_str()); }
        return false;
    }
    log("[nr] bridge: shared %s image %ux%u, DXGI format %u, VkFormat %u%s", what, w, h, unsigned(format),
        unsigned(image.format), image.storage ? "" : " (no storage use)");
    return true;
}

void Session::Impl::sweep_retired_images() {
    for (auto it = retired_images.begin(); it != retired_images.end();) {
        // Not before 16 more hand-overs, not while a job of ours (recorded or running) uses it, and a
        // D3D12 one not before its queue has got past the frames that copied out of it.
        if (bridge_runs < it->after || bdev->busy(it->image.image)) { ++it; continue; }
        if (it->mark == ~0ull) { it->mark = bdev->hand_over_mark(); ++it; continue; }
        ID3D12Resource* r12 = nullptr;
        const bool d3d12 = it->image.d3d && SUCCEEDED(it->image.d3d->QueryInterface(IID_PPV_ARGS(&r12))) && r12;
        if (r12) r12->Release();
        if (d3d12 && !bdev->hand_over_passed(it->mark)) { ++it; continue; }
        bdev->destroy(it->image);
        it = retired_images.erase(it);
    }
}

namespace {

void add_transition(std::vector<D3D12_RESOURCE_BARRIER>& v, ID3D12Resource* r, D3D12_RESOURCE_STATES from,
                    D3D12_RESOURCE_STATES to) {
    if (!r || from == to) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    v.push_back(b);
}

void flush_barriers(ID3D12GraphicsCommandList* list, std::vector<D3D12_RESOURCE_BARRIER>& v) {
    if (!v.empty()) list->ResourceBarrier(UINT(v.size()), v.data());
    v.clear();
}

ID3D12Resource* d3d12_of(const bridge::Image& image) { return static_cast<ID3D12Resource*>(image.d3d); }

ColourFrame bridge_frame(const bridge::Image& image) {
    ColourFrame f{};
    f.image = image.image;
    f.format = image.format;
    f.width = image.width;
    f.height = image.height;
    f.before = f.after = VK_IMAGE_LAYOUT_GENERAL;
    f.usage = image.usage;
    return f;
}

}  // namespace

ID3D12GraphicsCommandList* Session::Impl::next_own_list(ID3D12Device* device) {
    if (!own_lists_fence &&
        FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&own_lists_fence))))
        return nullptr;
    const uint64_t completed = own_lists_fence->GetCompletedValue();
    if (completed == UINT64_MAX) { if (bdev) bdev->lost("the game's D3D12 device was removed"); return nullptr; }
    OwnList* pick = nullptr;
    for (auto& l : own_lists)
        if (l.done <= completed && l.done < ~0ull - 1) { pick = &l; break; }
    if (!pick && own_lists.size() >= 8) {
        // Every one is still on the GPU: wait for the oldest, at most a second.
        OwnList* oldest = &own_lists[0];
        for (auto& l : own_lists) if (l.done < oldest->done) oldest = &l;
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event && SUCCEEDED(own_lists_fence->SetEventOnCompletion(oldest->done, event)))
            WaitForSingleObject(event, 1000);
        if (event) CloseHandle(event);
        if (own_lists_fence->GetCompletedValue() < oldest->done) return nullptr;
        pick = oldest;
    }
    if (pick) {
        if (FAILED(pick->allocator->Reset()) || FAILED(pick->list->Reset(pick->allocator, nullptr))) return nullptr;
    } else {
        OwnList l;
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&l.allocator))) ||
            FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, l.allocator, nullptr,
                                             IID_PPV_ARGS(&l.list)))) {
            if (l.allocator) l.allocator->Release();
            return nullptr;
        }
        own_lists.push_back(l);
        pick = &own_lists.back();
    }
    pick->done = ~0ull;   // recording; given its value when executed
    return pick->list;
}

void Session::Impl::release_bridge() {
    if (!bdev) return;
    bdev->wait_idle();
    if (own_lists_fence) {
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event && SUCCEEDED(own_lists_fence->SetEventOnCompletion(own_lists_value, event)))
            WaitForSingleObject(event, 1000);
        if (event) CloseHandle(event);
    }
    for (auto& l : own_lists) {
        if (l.list) l.list->Release();
        if (l.allocator) l.allocator->Release();
    }
    own_lists.clear();
    if (own_lists_fence) { own_lists_fence->Release(); own_lists_fence = nullptr; }
    // The D3D sides are released once the game's queue is past what it was given so far (bury): a
    // command list recorded with our copies can still be executed after the session is gone.
    bdev->bury(shared_colour);
    bdev->bury(shared_motion);
    bdev->bury(shared_depth);
    bdev->bury(shared_exposure);
    for (auto& r : retired_images) bdev->bury(r.image);
    retired_images.clear();
    for (auto& p : parked_sets) { bdev->bury(p.colour); bdev->bury(p.motion); bdev->bury(p.depth); }
    parked_sets.clear();
    if (own_queue) { own_queue->Release(); own_queue = nullptr; }
}

bridge::Job* Session::Impl::begin_job(std::string* why) {
    {
        std::lock_guard<std::mutex> guard(drops->m);
        if (drops->runtimes.erase(runtime)) {
            runtime->drop_history();
            log("[nr] bridge: a recorded frame was not run; the network's history starts again");
        }
    }
    bridge::Job* job = bdev->begin(why, runtime);
    if (job) {
        auto box = drops;
        const void* owner = runtime;
        bdev->on_drop(job, [box, owner] {
            std::lock_guard<std::mutex> guard(box->m);
            box->runtimes.insert(owner);
        });
    }
    return job;
}

bridge::Job* Session::Impl::record_bridge_job(const EngineFrame& base, bool motion, bool depth,
                                              const Controls& controls, const BridgeRenoDX* renodx) {
    (void)depth;
    std::string why;
    bridge::Job* job = begin_job(&why);
    if (!job) {
        status = "neural rendering skipped: " + why;
        return nullptr;
    }
    std::vector<const bridge::Image*> images{&shared_colour};
    if (base.motion.image) images.push_back(&shared_motion);
    if (base.depth.image) images.push_back(&shared_depth);
    if (renodx && renodx->exposure) images.push_back(&shared_exposure);
    bdev->acquire(job, images);
    if (renodx) {
        // In place on the shared colour image; record_renodx sets `status` (and `failed`) when it cannot.
        const ColourFrame exposure = bridge_frame(shared_exposure);
        if (!record_renodx(bdev->command_buffer(job), *renodx->stage, base.colour, base.colour, base, renodx->controls,
                           *renodx->frame, renodx->exposure ? &exposure : nullptr)) {
            bdev->abort(job);
            return nullptr;
        }
        if (!bdev->end(job, &why)) {
            status = why;
            return nullptr;
        }
        return job;
    }
    try {
        const VkCommandBuffer cmd = bdev->command_buffer(job);
        if (motion) runtime->record_engine(cmd, base, controls);
        else runtime->record(cmd, base.colour, controls, base.reset);
    } catch (const std::exception& e) {
        bdev->abort(job);
        status = std::string("neural rendering failed: ") + e.what();
        failed = true;
        log("[nr] %s", status.c_str());
        return nullptr;
    }
    if (!bdev->end(job, &why)) {
        status = why;
        return nullptr;
    }
    return job;
}

// The game's frame, motion and depth into the shared images, recorded into `list`. Each resource goes
// back to the state it came in; the shared ones end in COMMON, as every hand-over to the other API
// wants. `depth` comes back false when the depth buffer could not be read.
void Session::Impl::record_copy_in(ID3D12Device* device, ID3D12GraphicsCommandList* list,
                                   ID3D12Resource* colour, D3D12_RESOURCE_STATES colour_state, ID3D12Resource* motion,
                                   D3D12_RESOURCE_STATES motion_state, ID3D12Resource* depth,
                                   D3D12_RESOURCE_STATES depth_state, ID3D12Resource* exposure,
                                   D3D12_RESOURCE_STATES exposure_state) {
    auto& s = *this;
    constexpr auto kCommon = D3D12_RESOURCE_STATE_COMMON;
    constexpr auto kSrc = D3D12_RESOURCE_STATE_COPY_SOURCE, kDst = D3D12_RESOURCE_STATE_COPY_DEST;
    constexpr auto kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    constexpr auto kUav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ID3D12Resource* sc = d3d12_of(s.shared_colour);
    ID3D12Resource* sm = motion ? d3d12_of(s.shared_motion) : nullptr;
    ID3D12Resource* sd = depth ? d3d12_of(s.shared_depth) : nullptr;
    ID3D12Resource* se = exposure ? d3d12_of(s.shared_exposure) : nullptr;
    // A read state gains the compute-read bit; a write state (or none) is left for compute reads alone,
    // since read and write bits cannot be combined.
    const auto kWrites = D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_UNORDERED_ACCESS |
                             D3D12_RESOURCE_STATE_DEPTH_WRITE | D3D12_RESOURCE_STATE_STREAM_OUT |
                             D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_RESOLVE_DEST;
    const D3D12_RESOURCE_STATES depth_read =
        (depth_state & kRead) ? depth_state
        : (depth_state & kWrites) || depth_state == D3D12_RESOURCE_STATE_COMMON ? kRead
                                                                                : (depth_state | kRead);
    std::vector<D3D12_RESOURCE_BARRIER> b;
    add_transition(b, colour, colour_state, kSrc);
    add_transition(b, sc, kCommon, kDst);
    if (motion) { add_transition(b, motion, motion_state, kSrc); add_transition(b, sm, kCommon, kDst); }
    if (depth) { add_transition(b, depth, depth_state, depth_read); add_transition(b, sd, kCommon, kUav); }
    if (exposure) { add_transition(b, exposure, exposure_state, kSrc); add_transition(b, se, kCommon, kDst); }
    flush_barriers(list, b);
    list->CopyResource(sc, colour);
    if (motion) list->CopyResource(sm, motion);
    if (exposure) list->CopyResource(se, exposure);
    if (depth) {
        std::string why;
        // Checked by bridge_prepare_d3d12 already (depth12.ready); a failure here would only leave the
        // depth image as it was.
        if (!s.depth12.record(device, list, depth, sd, &why)) log("[nr] bridge: depth copy failed: %s", why.c_str());
    }
    add_transition(b, colour, kSrc, colour_state);
    add_transition(b, sc, kDst, kCommon);
    if (motion) { add_transition(b, motion, kSrc, motion_state); add_transition(b, sm, kDst, kCommon); }
    if (depth) { add_transition(b, depth, depth_read, depth_state); add_transition(b, sd, kUav, kCommon); }
    if (exposure) { add_transition(b, exposure, kSrc, exposure_state); add_transition(b, se, kDst, kCommon); }
    flush_barriers(list, b);
}

// The enhanced frame back into the game's resource.
void Session::Impl::record_copy_out(ID3D12GraphicsCommandList* list, ID3D12Resource* target,
                                    D3D12_RESOURCE_STATES target_state) {
    auto& s = *this;
    ID3D12Resource* sc = d3d12_of(s.shared_colour);
    std::vector<D3D12_RESOURCE_BARRIER> b;
    add_transition(b, sc, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    add_transition(b, target, target_state, D3D12_RESOURCE_STATE_COPY_DEST);
    flush_barriers(list, b);
    list->CopyResource(target, sc);
    add_transition(b, sc, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    add_transition(b, target, D3D12_RESOURCE_STATE_COPY_DEST, target_state);
    flush_barriers(list, b);
}

// Everything up to and including the network's recording: the runtime, the shared images, the copy
// into them (into `copy_list`) and the job. Null with `status` set when the frame passes through.
namespace {
// A resource the bridge copies whole with CopyResource into its one-mip, one-layer, single-sample
// shared image.
bool plain_2d(const D3D12_RESOURCE_DESC& d) {
    return d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.MipLevels == 1 && d.DepthOrArraySize == 1 &&
           d.SampleDesc.Count == 1;
}
}  // namespace

void Session::Impl::select_shared_set(uint32_t w, uint32_t h, DXGI_FORMAT fmt, const void* device) {
    if (!shared_colour || shared_colour.matches(w, h, fmt, device)) return;
    // Features at two extents (before and after the upscale) take turns: each keeps its own images
    // rather than recreating them on every switch.
    size_t found = parked_sets.size();
    for (size_t i = 0; i < parked_sets.size(); ++i)
        if (parked_sets[i].colour.matches(w, h, fmt, device)) found = i;
    SharedSet current{shared_colour, shared_motion, shared_depth};
    shared_colour = bridge::Image{}; shared_motion = bridge::Image{}; shared_depth = bridge::Image{};
    if (found < parked_sets.size()) {
        shared_colour = parked_sets[found].colour;
        shared_motion = parked_sets[found].motion;
        shared_depth = parked_sets[found].depth;
        parked_sets.erase(parked_sets.begin() + long(found));
    }
    parked_sets.push_back(current);
    if (parked_sets.size() > 3) {
        for (bridge::Image* i : {&parked_sets[0].colour, &parked_sets[0].motion, &parked_sets[0].depth})
            if (*i) retired_images.push_back({*i, bridge_runs + 16});
        parked_sets.erase(parked_sets.begin());
    }
}

bool Session::Impl::bridge_renodx(const Session::RenoDXFrame& rf, uint64_t feature, uint32_t w, uint32_t h,
                                  VkFormat format, uint32_t guide_w, uint32_t guide_h, const Controls& controls,
                                  BridgeRenoDX* out) {
    uint32_t nw = w, nh = h;
    renodx_extent(feature, w, h, guide_w, guide_h, rf.settings, &nw, &nh);
    out->frame = &rf;
    if (!renodx_runtime(controls, nw, nh, &out->controls)) return false;
    out->stage = renodx_stage(feature, w, h, nw, nh, format, rf);
    return out->stage != nullptr;
}

bridge::Job* Session::Impl::bridge_prepare_d3d12(ID3D12Device* device, ID3D12GraphicsCommandList* copy_list,
                                                 const BridgeInputs& in, const Controls& controls,
                                                 const std::function<bool()>& before_copy) {
    auto& s = *this;
    ++s.bridge_runs;
    s.sweep_retired_images();
    const D3D12_RESOURCE_DESC desc = in.target->GetDesc();
    const DXGI_FORMAT fmt = bridge::shared_format(desc.Format);
    const VkFormat vk = fmt == DXGI_FORMAT_UNKNOWN ? VK_FORMAT_UNDEFINED : vulkan_colour_format_of(fmt);
    if (vk == VK_FORMAT_UNDEFINED) {
        s.status = "the frame's DXGI format " + std::to_string(unsigned(desc.Format)) + " is not one the bridge carries";
        return nullptr;
    }
    if (!plain_2d(desc)) {
        s.status = "the frame is not a single-sample 2D texture with one mip and one layer";
        return nullptr;
    }
    if (in.source) {
        const D3D12_RESOURCE_DESC sd = in.source->GetDesc();
        if (bridge::shared_format(sd.Format) != fmt || sd.Width != desc.Width || sd.Height != desc.Height ||
            !plain_2d(sd)) {
            s.status = "the input is not the output's twin";
            return nullptr;
        }
    }
    const uint32_t w = uint32_t(desc.Width), h = desc.Height;
    BridgeRenoDX renodx{};
    if (in.renodx) {
        if (!in.motion && Impl::renodx_needs_motion(*in.renodx)) {
            s.status = "the RenoDX-style option needs the motion vectors";
            return nullptr;
        }
        const D3D12_RESOURCE_DESC md = in.motion ? in.motion->GetDesc() : D3D12_RESOURCE_DESC{};
        if (!s.bridge_renodx(*in.renodx, in.feature, w, h, vk,
                             in.motion_subrect.width ? in.motion_subrect.width : uint32_t(md.Width),
                             in.motion_subrect.height ? in.motion_subrect.height : md.Height, controls, &renodx))
            return nullptr;
    } else if (!s.ensure_runtime(controls, w, h, vk, in.linear && Session::Impl::is_linear_format(vk))) {
        return nullptr;
    }
    s.select_shared_set(w, h, fmt, device);
    if (!s.ensure_shared(s.shared_colour, w, h, fmt, device, nullptr, "colour")) return nullptr;
    // Everything that decides what is copied is settled here, before anything is recorded anywhere.
    bool motion = false, depth = false;
    if (in.motion) {
        const D3D12_RESOURCE_DESC md = in.motion->GetDesc();
        const DXGI_FORMAT mf = bridge::shared_format(md.Format);
        motion = mf != DXGI_FORMAT_UNKNOWN && plain_2d(md) &&
                 s.ensure_shared(s.shared_motion, uint32_t(md.Width), md.Height, mf, device, nullptr, "motion");
    }
    if (in.renodx && !motion && Impl::renodx_needs_motion(*in.renodx)) {
        s.status = "the RenoDX-style option needs the motion vectors, and the bridge cannot carry them";
        return nullptr;
    }
    // RenoDX's exposure feed: the game's exposure texture (texel 0,0), when there is one the bridge can carry.
    if (in.renodx && in.exposure) {
        const D3D12_RESOURCE_DESC ed = in.exposure->GetDesc();
        const DXGI_FORMAT ef = bridge::shared_format(ed.Format);
        renodx.exposure = ef != DXGI_FORMAT_UNKNOWN && plain_2d(ed) &&
                          s.ensure_shared(s.shared_exposure, uint32_t(ed.Width), ed.Height, ef, device, nullptr,
                                          "exposure");
    }
    if (in.depth) {
        const D3D12_RESOURCE_DESC dd = in.depth->GetDesc();
        std::string why;
        if (dd.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || dd.DepthOrArraySize != 1 || dd.SampleDesc.Count != 1)
            why = "the depth buffer is not a single-sample 2D texture";
        else if (dd.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)
            why = "the depth buffer denies shader reads";
        else if (bridge::depth_view_format(dd.Format) == DXGI_FORMAT_UNKNOWN)
            why = "the depth buffer's DXGI format " + std::to_string(unsigned(dd.Format)) + " cannot be read";
        else if (!s.depth12.ready(device, &why)) {
        } else if (!s.ensure_shared(s.shared_depth, uint32_t(dd.Width), dd.Height, DXGI_FORMAT_R32_FLOAT, device,
                                    nullptr, "depth")) {
            why = s.status;
        } else if (!s.shared_depth.storage) {
            why = "the shared depth image cannot be written by a shader";
        } else {
            depth = true;
        }
        if (!depth && !s.depth_warned) {
            s.depth_warned = true;
            log("[nr] bridge: running without depth: %s", why.c_str());
        }
    }
    s.status.clear();
    // Everything that can still turn the frame away is asked before the network is recorded: a recording
    // advances the network's history, and one that never runs leaves that history unwritten.
    if (before_copy && !before_copy()) return nullptr;

    // The network first: a frame that cannot be recorded leaves the game's list untouched.
    EngineFrame engine{};
    engine.colour = bridge_frame(s.shared_colour);
    engine.feature = in.feature;
    engine.reset = in.reset;
    engine.motion_scale_x = in.motion_scale_x;
    engine.motion_scale_y = in.motion_scale_y;
    engine.depth_inverted = in.depth_inverted;
    if (motion) {
        engine.motion = bridge_frame(s.shared_motion);
        engine.motion_texture_width = engine.motion.width;
        engine.motion_texture_height = engine.motion.height;
        apply_guide_subrect(&engine.motion, in.motion_subrect, "motion-vector", &engine.motion_x, &engine.motion_y);
    }
    if (depth) {
        engine.depth = bridge_frame(s.shared_depth);
        apply_guide_subrect(&engine.depth, in.depth_subrect, "depth", &engine.depth_x, &engine.depth_y);
    }
    bridge::Job* job = s.record_bridge_job(engine, motion, depth, controls, in.renodx ? &renodx : nullptr);
    if (!job) return nullptr;
    record_copy_in(device, copy_list, in.source ? in.source : in.target, in.source ? in.source_state : in.target_state,
                   motion ? in.motion : nullptr, in.motion_state, depth ? in.depth : nullptr, in.depth_state,
                   renodx.exposure ? in.exposure : nullptr, in.exposure_state);
    return job;
}

Session::Session(std::string root) : impl_(std::make_unique<Impl>()) {
    impl_->root = root.empty() ? std::string(nr::pe::module_folder()) : std::move(root);
    // The runtime's own diagnostics - the build's phase breakdown above all -
    // went to a stdout a game does not have. Send them to the same file
    // everything else here writes to.
    nr::set_log_sink([](const char* line) { log("%s", line); });
    // 32-bit: the build logs the free address space at each phase.
    if (sizeof(void*) == 4) nr::g_memory_note = [] { return address_space_report(); };
    log("[nr] session root %s", impl_->root.c_str());
    // NR_INPUT_CHECK=1 (NR_INPUT_CHECK_DEFAULT for a build that has it on without asking):
    // score what the game hands the network and what it gets back; pictures in
    // dlssnr-amd-check\ beside the log. NR_INPUT_CHECK_PICTURES=n sets how many captures
    // are written as pictures (default 4).
    static const bool input_check = [] {
#ifdef NR_INPUT_CHECK_DEFAULT
        bool on = NR_INPUT_CHECK_DEFAULT != 0;
#else
        bool on = false;
#endif
        char v[16] = {};
        if (GetEnvironmentVariableA("NR_INPUT_CHECK", v, sizeof v) > 0) on = v[0] == '1';
        if (!on) return false;
        int pictures = 4;
        if (GetEnvironmentVariableA("NR_INPUT_CHECK_PICTURES", v, sizeof v) > 0) pictures = std::atoi(v);
        std::string folder = nr::pe::module_folder();
        folder += folder.empty() ? "dlssnr-amd-check" : "\\dlssnr-amd-check";
        if (!CreateDirectoryA(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) folder.clear();
        nr::set_input_check(true, folder, pictures);
        return true;
    }();
    (void)input_check;
}

void Session::set_white_point(float v) {
    impl_->white_point = v;
    for (auto& e : impl_->cache) e.runtime->set_white_point(v);
}

void Session::set_native_compose(bool v) { impl_->native_compose = v; }

void Session::set_max_passes(uint32_t v) {
    // One pass: a network built for exactly one, whose single-pass paths (history
    // written in turn into two images, motion sampled in place, the input not
    // kept for later passes) do less work for the same answer. More than one:
    // never below four, so 2..4 share a network; above it the next frame
    // rebuilds, like a resolution change. A change of pass count keeps the
    // running network serving until the new one is built (ensure_runtime_).
    const uint32_t want = v <= 1 ? 1u
                        : v < Session::Impl::kMaxPasses ? Session::Impl::kMaxPasses
                        : (v > Session::Impl::kPassCeiling ? Session::Impl::kPassCeiling : v);
    impl_->max_passes_want = want;
}

void Session::set_model_scale(float v) {
    // Clamped to the range the runtime accepts; the rebuild happens on the
    // next frame, in the background, like a resolution change.
    impl_->model_scale = v < 0.25f ? 0.25f : (v > 1.0f ? 1.0f : v);
}

void Session::set_history_strength(float v) {
    impl_->history_strength = v;
    for (auto& e : impl_->cache) e.runtime->set_history_strength(v);
}

Session::~Session() {
    // A build still running owns nothing the session is about to destroy, but
    // it will hand back a runtime that must be destroyed too: wait for it.
    if (impl_->build_thread.joinable()) impl_->build_thread.join();
    // On the bridge: frames recorded for these networks and not yet run are given up (a command list
    // that runs later skips them), and our queue may still be running frames that use them.
    if (impl_->bdev) {
        for (auto& e : impl_->cache) impl_->bdev->cancel(e.runtime.get());
        for (auto& e : impl_->retiring) impl_->bdev->cancel(e.entry.runtime.get());
        for (auto& e : impl_->retired) impl_->bdev->cancel(e.runtime.get());
        impl_->bdev->wait_idle();
    }
    impl_->built.reset();
    impl_->runtime = nullptr;
    impl_->cache.clear();
    impl_->retiring.clear();
    impl_->retired.clear();
    impl_->release_output();
    impl_->release_invalidator();
    impl_->release_bridge();
    impl_->release_vk_output();
    if (impl_->stage_timing) vkDestroyQueryPool(impl_->handles.device, impl_->stage_timing, nullptr);
    // Screenshot readbacks: the worker may still be reading one it was handed (it finishes on its own).
    if (impl_->shot) impl_->shot_free(*impl_->shot);
    for (auto& sh : impl_->shots_out) {
        for (int i = 0; i < 500 && sh->handed && !sh->read.load(std::memory_order_acquire); ++i) Sleep(10);
        if (!sh->handed || sh->read.load(std::memory_order_acquire)) impl_->shot_free(*sh);
    }
    if (impl_->neutral_mv) vkDestroyImage(impl_->handles.device, impl_->neutral_mv, nullptr);
    if (impl_->neutral_mv_memory) vkFreeMemory(impl_->handles.device, impl_->neutral_mv_memory, nullptr);
    if (impl_->pool) vkDestroyCommandPool(impl_->handles.device, impl_->pool, nullptr);
    if (impl_->owned_queue) impl_->owned_queue->Release();
    if (impl_->dxvk) impl_->dxvk->Release();
    if (impl_->own_fence) {
        // Nothing may be freed while the GPU is still reading it.
        impl_->wait_for_own_cmd();
        vkDestroyFence(impl_->handles.device, impl_->own_fence, nullptr);
    }
    impl_->release_slots();
}

void Session::wait_idle() {
    impl_->wait_for_own_cmd();
    for (auto& slot : impl_->slots) impl_->wait_slot(slot);
    if (impl_->bdev) impl_->bdev->wait_idle();
}

const std::string& Session::status() const { return impl_->status; }

bool Session::building() const { return impl_->building; }
bool Session::failed() const { return impl_->failed; }

// The runtime that is actually running frames, not the one being built: during a
// rebuild the old one is still the one paying for the picture on screen.
float Session::gpu_ms() const {
    if (impl_->renodx_timed) return impl_->stage_ms_avg;
    return impl_->runtime ? impl_->runtime->average_gpu_ms() : 0.0f;
}

float Session::network_ms() const {
    if (impl_->renodx_timed) return impl_->stage_net_ms_avg;
    return impl_->runtime ? impl_->runtime->average_network_ms() : 0.0f;
}

void Session::network_size(uint32_t* width, uint32_t* height) const {
    *width = impl_->runtime ? impl_->runtime->model_width() : 0;
    *height = impl_->runtime ? impl_->runtime->model_height() : 0;
}

Session::State Session::state() const {
    State s;
    const auto& m = *impl_;
    s.running = m.runtime != nullptr;
    if (m.runtime) { s.model_w = m.runtime->model_width(); s.model_h = m.runtime->model_height(); }
#if NR_INT4
    s.running_int4 = m.runtime ? m.running_int4 : -1;
    s.want_int4 = m.int4_want;
    s.int4_available = !m.int4_switch.is_off();
    s.building_int4 = m.building ? m.build_int4 : -1;
#endif
    s.building = m.building;
    if (m.building) {
        s.building_seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - m.build_started).count();
        s.stage = nr::g_build_stage;
        s.pipes_done = nr::g_build_pipes_done; s.pipes_total = nr::g_build_pipes_total;
    }
    return s;
}

void Session::set_int4(bool on) {
#if NR_INT4
    impl_->int4_switch.set(on);
#else
    (void)on;
#endif
}

std::string Session::vram_line() const {
    const MemoryState m = read_memory(impl_->handles.physical);
    char line[128];
    if (m.vram_known)
        std::snprintf(line, sizeof line, "VRAM %.0f / %.0f MB, commit %.0f MB free of %.0f",
                      mb(m.vram_used), mb(m.vram_budget), mb(m.commit_free), mb(m.commit_limit));
    else
        std::snprintf(line, sizeof line, "commit %.0f MB free of %.0f", mb(m.commit_free), mb(m.commit_limit));
    return line;
}

uint64_t Session::create_feature() {
    const uint64_t id = impl_->next_feature++;
    log("[nr] feature #%u created", static_cast<unsigned>(id));
    return id;
}

void Session::release_feature(uint64_t feature) {
    if (!feature) return;
    // Every network this feature may have been evaluated against holds a
    // history for it. None of them is freed here; see Runtime::release_feature.
    for (auto& e : impl_->cache) e.runtime->release_feature(feature);
    const auto stage = impl_->renodx.find(feature);
    if (stage != impl_->renodx.end()) {
        for (uint32_t k = 1; k < stage->second.stack; ++k) impl_->release_everywhere(Impl::stack_feature(feature, k));
        impl_->retire_stage(std::move(stage->second.stage));
        impl_->renodx.erase(stage);
    }
    log("[nr] feature #%u released", static_cast<unsigned>(feature));
}

ID3D12Resource* Session::run(ID3D12Device* device, ID3D12GraphicsCommandList* list,
                             const EngineResources& resources, const Controls& controls) {
    auto& s = *impl_;
    ID3D12Resource* const colour = resources.colour;
    const D3D12_RESOURCE_STATES colour_state = resources.colour_state;
    if (s.failed || !device || !list || !colour) return nullptr;
    s.status.clear();

    if (!s.ensure_d3d12_device(device)) return nullptr;
    const VkCommandBuffer cmd = command_buffer(list);
    if (!cmd) {
        s.status = "the command list exposes no Vulkan handle";
        s.failed = true;
        log("[nr] %s", s.status.c_str());
        return nullptr;
    }
    const auto colour_vk = colour_handle(device, colour, colour_state);
    if (!colour_vk.usable()) {
        s.status = describe_rejection(colour_vk, "colour");
        return nullptr;
    }
    if (resource_is_typeless(colour_vk) && !s.typeless_noted) {
        s.typeless_noted = true;
        log("[nr] the colour resource is typeless (DXGI format %u); read through vkd3d's "
            "representative format %u - if the picture is wrong, start here",
            unsigned(colour_vk.dxgi), unsigned(colour_vk.format));
    }
    const auto colour_desc = colour->GetDesc();
    if (!s.ensure_output(device, colour_vk.width, colour_vk.height, colour_desc.Format))
        return nullptr;

    // The network's extent is the *render* resolution, which changes with the
    // upscaler's quality preset. Rebuild rather than scale: this pass is only
    // cheap because it runs at exactly the size the game rendered.
    if (!s.ensure_runtime(controls, colour_vk.width, colour_vk.height, colour_vk.format,
                          !resources.colour_encoded &&
                              Session::Impl::is_linear_format(colour_vk.format)))
        return nullptr;

    // The game keeps using its own colour buffer, so the network works on our
    // copy and the upscaler is pointed at that instead.
    ColourFrame frame{};
    frame.image = s.output_vk.image;
    frame.format = colour_vk.format;
    frame.width = colour_vk.width; frame.height = colour_vk.height;
    frame.before = frame.after = VK_IMAGE_LAYOUT_GENERAL;

    EngineFrame engine{};
    engine.colour = frame;
    engine.feature = resources.feature;
    engine.reset = resources.reset;
    engine.motion_scale_x = resources.motion_scale_x;
    engine.motion_scale_y = resources.motion_scale_y;
    engine.depth_inverted = resources.depth_inverted;
    const bool motion_vk = resources.motion &&
        fill(&engine.motion, resource_handle(device, resources.motion, resources.motion_state));
    if (motion_vk) {
        engine.motion_texture_width = engine.motion.width; engine.motion_texture_height = engine.motion.height;
        apply_guide_subrect(&engine.motion, resources.motion_subrect, "motion-vector", &engine.motion_x, &engine.motion_y);
    }
    if (resources.depth &&
        fill(&engine.depth, resource_handle(device, resources.depth, resources.depth_state)))
        apply_guide_subrect(&engine.depth, resources.depth_subrect, "depth", &engine.depth_x, &engine.depth_y);

    try {
        // Seed our output with the game's colour, then enhance it in place.
        VkImageCopy copy{};
        copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.extent = {colour_vk.width, colour_vk.height, 1};
        // The caller told us the D3D12 state; vkd3d told us the layout that
        // implies. Transition from it and put it back, or the game's next use
        // of its own buffer sees a layout it never asked for.
        VkImageMemoryBarrier in{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        in.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        in.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        in.oldLayout = colour_vk.layout; in.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        in.srcQueueFamilyIndex = in.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        in.image = colour_vk.image;
        in.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageMemoryBarrier out = in;
        out.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        out.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        out.oldLayout = VK_IMAGE_LAYOUT_GENERAL; out.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        out.image = s.output_vk.image;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &in);
        vkCmdCopyImage(cmd, colour_vk.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       s.output_vk.image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
        std::swap(in.oldLayout, in.newLayout);
        in.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        in.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &in);
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &out);

        if (motion_vk) s.runtime->record_engine(cmd, engine, controls);
        else s.runtime->record(cmd, frame, controls, engine.reset);
    } catch (const std::exception& e) {
        s.status = std::string("neural rendering failed: ") + e.what();
        s.failed = true;
        log("[nr] %s", s.status.c_str());
        return nullptr;
    }
    return s.output;
}

bool Session::run_after(ID3D12Device* device, ID3D12GraphicsCommandList* list,
                        ID3D12Resource* output, D3D12_RESOURCE_STATES output_state,
                        const EngineResources& resources, const Controls& controls,
                        ID3D12Resource* input, D3D12_RESOURCE_STATES input_state) {
    auto& s = *impl_;
    if (s.failed || !device || !list || !output) return false;
    s.status.clear();
    if (bridged(device)) {
        // Native D3D12: the list is cut here (nr_pe_d3d12split.hpp) and the network runs between the
        // halves on our own device.
        if (!s.ensure_bridge(device->GetAdapterLuid())) return false;
        BridgeInputs in{};
        in.target = output; in.target_state = output_state;
        // A separate input: copied into the shared image instead of the output, which then needs no
        // seed copy (the answer is copied into it either way).
        in.source = input; in.source_state = input_state;
        in.motion = resources.motion; in.motion_state = resources.motion_state;
        in.depth = resources.depth; in.depth_state = resources.depth_state;
        in.motion_subrect = resources.motion_subrect; in.depth_subrect = resources.depth_subrect;
        in.motion_scale_x = resources.motion_scale_x; in.motion_scale_y = resources.motion_scale_y;
        in.depth_inverted = resources.depth_inverted;
        in.reset = resources.reset;
        in.feature = resources.feature;
        in.linear = !resources.colour_encoded;
        std::shared_ptr<split::Snapshot> state;
        std::string why;
        // The game's state as the evaluate found it, before anything of ours is recorded.
        const auto before_copy = [&] {
            if (!split::prepare(list, &why)) { s.status = "the command list cannot be cut yet: " + why; return false; }
            state = split::snapshot(list);
            if (!state) { s.status = "the command list's state is not known"; return false; }
            return true;
        };
        bridge::Job* job = s.bridge_prepare_d3d12(device, list, in, controls, before_copy);
        if (!job) return false;
        bridge::Device* d = s.bdev;
        const uint64_t gen = d->generation(job);
        split::Job between;
        between.run = [d, job, gen](ID3D12CommandQueue* queue) { d->run_between(queue, job, gen); };
        between.discard = [d, job, gen] { d->discard(job, gen); };
        if (!split::cut(list, std::move(between), state, &why)) {
            // The copy into the shared images is in the list; the bindings the depth copy made are
            // taken back, and the frame is left alone.
            d->discard(job, gen);
            split::restore(list, state);
            s.status = "the command list could not be cut: " + why;
            thread_local std::string said;
            if (said != s.status) { said = s.status; log("[nr] bridge: %s", s.status.c_str()); }
            return false;
        }
        // Recorded through the list, so into the continuation, after our work.
        s.record_copy_out(list, output, output_state);
        static bool first = true;
        if (first) {
            first = false;
            log("[nr] bridge: first frame cut at the evaluate and handed to the network (%ux%u)",
                unsigned(output->GetDesc().Width), output->GetDesc().Height);
        }
        return true;
    }
    if (!s.ensure_d3d12_device(device)) return false;
    const VkCommandBuffer cmd = command_buffer(list);
    if (!cmd) { s.status = "the command list exposes no Vulkan handle"; s.failed = true; return false; }
    const auto target = colour_handle(device, output, output_state);
    if (!target.usable()) { s.status = describe_rejection(target, "output"); return false; }
    if (!s.ensure_runtime(controls, target.width, target.height, target.format,
                          !resources.colour_encoded &&
                              Session::Impl::is_linear_format(target.format))) return false;

    // In place: the upscaler already wrote this and nobody is waiting for a
    // different resource, so there is nothing to rebind.
    ColourFrame frame{};
    frame.image = target.image;
    frame.format = target.format;
    frame.width = target.width; frame.height = target.height;
    frame.before = frame.after = target.layout;
    frame.usage = target.usage;

    EngineFrame engine{};
    engine.colour = frame;
    // A separate input: the network samples it in place and stores its answer
    // into the output, so the output needs no copy of the colour first. Only
    // the engine path with the model applied; anything else is the caller's
    // seed copy and the in-place run.
    if (input) {
        if (!resources.motion || !s.runtime || !s.runtime->takes_target(controls)) return false;
        const auto source = colour_handle(device, input, input_state);
        if (!source.usable() || source.format != target.format || source.width != target.width ||
            source.height != target.height || !(source.usage & VK_IMAGE_USAGE_SAMPLED_BIT))
            return false;
        engine.target = frame;
        engine.colour.image = source.image;
        engine.colour.before = engine.colour.after = source.layout;
        engine.colour.usage = source.usage;
    }
    engine.feature = resources.feature;
    engine.reset = resources.reset;
    engine.motion_scale_x = resources.motion_scale_x;
    engine.motion_scale_y = resources.motion_scale_y;
    engine.depth_inverted = resources.depth_inverted;
    const bool motion_vk = resources.motion &&
        fill(&engine.motion, resource_handle(device, resources.motion, resources.motion_state));
    if (motion_vk) {
        engine.motion_texture_width = engine.motion.width; engine.motion_texture_height = engine.motion.height;
        apply_guide_subrect(&engine.motion, resources.motion_subrect, "motion-vector", &engine.motion_x, &engine.motion_y);
    }
    if (resources.depth &&
        fill(&engine.depth, resource_handle(device, resources.depth, resources.depth_state)))
        apply_guide_subrect(&engine.depth, resources.depth_subrect, "depth", &engine.depth_x, &engine.depth_y);
    if (input && !motion_vk) return false;   // the plain path works in place: the caller seeds
    // Full memory barriers on either side of the network, because the caller's
    // barriers are vkd3d's translation of D3D12 resource states and know
    // nothing about ours. Its "UAV -> SRV" on the output waits for compute
    // shader writes; the network's last write into the output is a
    // vkCmdCopyImage - a TRANSFER-stage write - so a dispatch that follows
    // immediately (OptiScaler's clamp between two NR passes, its resolve) can
    // start before that copy has landed and reads what was there before: the
    // seed copy of the input. One pass survived only because other work
    // happened to sit between the two. The same in reverse for our reads.
    const auto everything = [&](VkAccessFlags src_access, VkAccessFlags dst_access) {
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask = src_access; mb.dstAccessMask = dst_access;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0, 1, &mb, 0, nullptr, 0, nullptr);
    };
    everything(VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    try {
        if (motion_vk) s.runtime->record_engine(cmd, engine, controls);
        else s.runtime->record(cmd, frame, controls, engine.reset);
    } catch (const std::exception& e) {
        s.status = std::string("neural rendering failed: ") + e.what();
        s.failed = true;
        log("[nr] %s", s.status.c_str());
        return false;
    }
    // The network was recorded as raw Vulkan into vkd3d-proton's command buffer:
    // it bound its own compute pipelines, descriptor sets and push constants.
    // vkd3d does not know, and it de-duplicates state on the D3D12 side -
    // SetPipelineState returns early when `list->state == state`, the root
    // signature likewise - so a caller that sets the SAME pipeline state
    // before and after us (OptiScaler's clamp between two NR passes, then its
    // resolve) never gets vkCmdBindPipeline re-emitted and dispatches our last
    // kernel instead of its own. NVIDIA's DLL never trips this because a cubin
    // launch (vkCmdCuLaunchKernelNVX) touches no Vulkan bind point. Setting the
    // compute bind point's tracked state to null makes the next real set differ
    // and vkd3d re-emits the pipeline, the descriptor offsets and the push
    // constants (SetComputeRootSignature -> invalidate_root_parameters). The
    // graphics bind point was not touched by us and is left alone.
    everything(VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    list->SetPipelineState(nullptr);
    list->SetComputeRootSignature(nullptr);
    // ...and the part those two cannot reach: vkd3d's memory of what is bound in
    // the Vulkan command buffer itself. Without this, the second neural pass in
    // one command list leaves every following caller dispatch running our kernel.
    s.forget_bound_pipeline(device, list);
    return true;
}

bool Session::bridged(ID3D12Device* device) {
    auto& s = *impl_;
    if (s.mode != Impl::Mode::Unknown) return s.mode == Impl::Mode::Bridge;
    return s.decide_mode(device_handles(device).valid());
}

bool Session::run_d3d12_queue(const D3D12QueueFrame& f, const Controls& controls) {
    auto& s = *impl_;
    if (s.failed || !f.device || !f.queue || !f.target) return false;
    s.status.clear();
    if (!bridged(f.device)) { s.status = "the queue-level hand-over is for the native D3D12 runtime"; return false; }
    if (!s.ensure_bridge(f.device->GetAdapterLuid())) return false;
    // Our lists and their completion fence follow one queue's order: frames on any other queue pass through.
    if (!s.own_queue) { s.own_queue = f.queue; f.queue->AddRef(); }
    if (f.queue != s.own_queue) { s.status = "the frame is on another D3D12 queue than the network's"; return false; }
    ID3D12GraphicsCommandList* in_list = s.next_own_list(f.device);
    if (!in_list) { s.status = "no command list of our own on the game's device"; return false; }
    BridgeInputs in{};
    in.target = f.target; in.target_state = f.target_state;
    in.motion = f.motion; in.motion_state = f.motion_state;
    in.depth = f.depth; in.depth_state = f.depth_state;
    in.motion_scale_x = f.motion_scale_x; in.motion_scale_y = f.motion_scale_y;
    in.depth_inverted = f.depth_inverted;
    in.reset = f.reset;
    in.linear = f.linear_hdr;
    in.renodx = f.renodx;
    // The list's slot is handed back as finished at once if the frame passes through.
    const auto give_back = [&](ID3D12GraphicsCommandList* l) {
        l->Close();
        for (auto& o : s.own_lists) if (o.list == l) o.done = 0;
    };
    bridge::Job* job = s.bridge_prepare_d3d12(f.device, in_list, in, controls, nullptr);
    if (!job) { give_back(in_list); return false; }
    const uint64_t gen = s.bdev->generation(job);
    ID3D12GraphicsCommandList* out_list = s.next_own_list(f.device);
    if (!out_list) {
        s.bdev->discard(job, gen);
        give_back(in_list);
        s.status = "no command list of our own on the game's device";
        return false;
    }
    s.record_copy_out(out_list, f.target, f.target_state);
    const HRESULT closed_in = in_list->Close(), closed_out = out_list->Close();
    if (FAILED(closed_in) || FAILED(closed_out)) {
        s.bdev->discard(job, gen);
        for (auto& o : s.own_lists) if (o.list == in_list || o.list == out_list) o.done = 0;
        s.status = "our command lists would not close";
        return false;
    }
    // Everything recorded before us goes to the queue first; then the copy in, the hand-over to our
    // queue and back, the copy out (only when the network ran: otherwise the frame stays as it was).
    if (f.flush) f.flush();
    ID3D12CommandList* first = in_list;
    f.queue->ExecuteCommandLists(1, &first);
    const bool ran = s.bdev->run_between(f.queue, job, gen);
    if (ran) {
        ID3D12CommandList* second = out_list;
        f.queue->ExecuteCommandLists(1, &second);
    }
    // The in-list copy is on the queue whatever happened: the slots are free once this value is reached.
    if (FAILED(f.queue->Signal(s.own_lists_fence, s.own_lists_value + 1))) {
        // No completion mark: these two lists are never handed out again.
        for (auto& o : s.own_lists) if (o.list == in_list || o.list == out_list) o.done = ~0ull - 1;
        s.status = "the D3D12 queue refused a signal";
        return false;
    }
    ++s.own_lists_value;
    for (auto& o : s.own_lists)
        if (o.list == in_list || o.list == out_list) o.done = ran || o.list == in_list ? s.own_lists_value : 0;
    if (!ran) { s.status = "the network did not run on this frame"; return false; }
    static bool first_frame = true;
    if (first_frame) { first_frame = false; log("[nr] bridge: first D3D12 frame handed over at queue level"); }
    return true;
}

namespace {
LUID adapter_luid(IUnknown* device) {
    LUID luid{};
    IDXGIDevice* dxgi = nullptr;
    IDXGIAdapter* adapter = nullptr;
    DXGI_ADAPTER_DESC ad{};
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) && dxgi && SUCCEEDED(dxgi->GetAdapter(&adapter)) &&
        adapter && SUCCEEDED(adapter->GetDesc(&ad)))
        luid = ad.AdapterLuid;
    if (adapter) adapter->Release();
    if (dxgi) dxgi->Release();
    return luid;
}

// The keyed mutexes of the shared images, as one transaction: either every one is taken at `key`, or
// none is (those already taken are handed back at `key`).
enum class Keyed { Ok, Timeout, Abandoned, Failed };
Keyed acquire_all(const std::vector<const bridge::Image*>& images, UINT64 key, DWORD ms) {
    std::vector<IDXGIKeyedMutex*> taken;
    Keyed result = Keyed::Ok;
    for (const bridge::Image* i : images) {
        IDXGIKeyedMutex* km = nullptr;
        if (FAILED(i->d3d->QueryInterface(IID_PPV_ARGS(&km))) || !km) { result = Keyed::Failed; break; }
        const HRESULT hr = km->AcquireSync(key, ms);
        if (hr == S_OK) { taken.push_back(km); continue; }
        km->Release();
        result = hr == static_cast<HRESULT>(WAIT_ABANDONED) ? Keyed::Abandoned
               : hr == static_cast<HRESULT>(WAIT_TIMEOUT) ? Keyed::Timeout : Keyed::Failed;
        break;
    }
    for (IDXGIKeyedMutex* km : taken) {
        if (result != Keyed::Ok) km->ReleaseSync(key);
        km->Release();
    }
    return result;
}
// Every image handed over at `key`. False if any refused; the ones that refused are still ours (at the
// key they were acquired with), the others are at `key`.
bool release_all(const std::vector<const bridge::Image*>& images, UINT64 key, std::vector<bool>* released = nullptr) {
    bool ok = true;
    if (released) released->assign(images.size(), false);
    for (size_t n = 0; n < images.size(); ++n) {
        IDXGIKeyedMutex* km = nullptr;
        if (FAILED(images[n]->d3d->QueryInterface(IID_PPV_ARGS(&km))) || !km) { ok = false; continue; }
        const bool done = SUCCEEDED(km->ReleaseSync(key));
        km->Release();
        if (released) (*released)[n] = done;
        ok = ok && done;
    }
    return ok;
}
const char* keyed_text(Keyed k) {
    return k == Keyed::Timeout ? "timed out" : k == Keyed::Abandoned ? "was abandoned" : "failed";
}
}  // namespace

bool Session::run_d3d10(const D3D11Frame& frame, const Controls& controls) {
    auto& s = *impl_;
    if (s.failed || !frame.device || !frame.target) return false;
    s.status.clear();
    ID3D10Device* device = nullptr;
    if (FAILED(frame.device->QueryInterface(IID_PPV_ARGS(&device))) || !device) {
        s.status = "the device is not a D3D10 device";
        return false;
    }
    struct Release { IUnknown* p; ~Release() { if (p) p->Release(); } } hold_device{device};
    s.decide_mode(false);
    if (!s.ensure_bridge(adapter_luid(device))) return false;
    if (!s.bdev->keyed_mutex()) {
        s.status = "neural rendering unavailable in D3D10: the driver has no VK_KHR_win32_keyed_mutex";
        s.failed = true;
        log("[nr] %s", s.status.c_str());
        return false;
    }
    ++s.bridge_runs;
    s.sweep_retired_images();
    ID3D10Texture2D* target = nullptr;
    if (FAILED(frame.target->QueryInterface(IID_PPV_ARGS(&target))) || !target) {
        s.status = "the D3D10 frame is not a 2D texture";
        return false;
    }
    Release hold_target{target};
    D3D10_TEXTURE2D_DESC td{};
    target->GetDesc(&td);
    const DXGI_FORMAT fmt = bridge::shared_format(td.Format);
    const VkFormat vk = fmt == DXGI_FORMAT_UNKNOWN ? VK_FORMAT_UNDEFINED : vulkan_colour_format_of(fmt);
    if (vk == VK_FORMAT_UNDEFINED || td.SampleDesc.Count != 1 || td.MipLevels != 1 || td.ArraySize != 1) {
        s.status = "the frame's DXGI format " + std::to_string(unsigned(td.Format)) + " is not one the bridge carries";
        return false;
    }
    const bool linear = (frame.upscaler_input || frame.linear_hdr) && !frame.estimate_motion &&
                        Impl::is_linear_format(vk);
    BridgeRenoDX renodx{};
    if (frame.renodx) {
        if (!frame.motion && Impl::renodx_needs_motion(*frame.renodx)) {
            s.status = "the RenoDX-style option needs the motion vectors";
            return false;
        }
        D3D10_TEXTURE2D_DESC md{};
        ID3D10Texture2D* m = nullptr;
        if (frame.motion && SUCCEEDED(frame.motion->QueryInterface(IID_PPV_ARGS(&m))) && m) { m->GetDesc(&md); m->Release(); }
        if (!s.bridge_renodx(*frame.renodx, 0, td.Width, td.Height, vk, md.Width, md.Height, controls, &renodx))
            return false;
    } else if (!s.ensure_runtime(controls, td.Width, td.Height, vk, linear)) {
        return false;
    }
    if (!s.ensure_shared(s.shared_colour, td.Width, td.Height, fmt, nullptr, nullptr, "colour", device))
        return false;
    ID3D10Texture2D* motion = nullptr;
    if (frame.motion && SUCCEEDED(frame.motion->QueryInterface(IID_PPV_ARGS(&motion))) && motion) {
        D3D10_TEXTURE2D_DESC md{};
        motion->GetDesc(&md);
        const DXGI_FORMAT mf = bridge::shared_format(md.Format);
        if (mf == DXGI_FORMAT_UNKNOWN || md.SampleDesc.Count != 1 || md.MipLevels != 1 || md.ArraySize != 1 ||
            !s.ensure_shared(s.shared_motion, md.Width, md.Height, mf, nullptr, nullptr, "motion", device)) {
            motion->Release();
            motion = nullptr;
        }
    }
    Release hold_motion{motion};
    if (frame.renodx && !motion && Impl::renodx_needs_motion(*frame.renodx)) {
        s.status = "the RenoDX-style option needs the motion vectors, and the bridge cannot carry them";
        return false;
    }
    if (frame.depth && !s.depth_warned) {
        s.depth_warned = true;
        log("[nr] bridge: D3D10 has no compute shaders; the pass runs without depth");
    }
    s.status.clear();

    // The network first, so a frame that cannot be recorded leaves the game's texture alone.
    EngineFrame engine{};
    engine.colour = bridge_frame(s.shared_colour);
    engine.reset = frame.reset;
    engine.motion_scale_x = frame.motion_scale_x;
    engine.motion_scale_y = frame.motion_scale_y;
    engine.depth_inverted = frame.depth_inverted;
    if (motion) engine.motion = bridge_frame(s.shared_motion);
    bridge::Job* job = nullptr;
    if (frame.renodx) {
        job = s.record_bridge_job(engine, motion != nullptr, false, controls, &renodx);
        if (!job) return false;
    } else if (frame.estimate_motion && !motion) {
        std::string why;
        job = s.begin_job(&why);
        if (!job) { s.status = "neural rendering skipped: " + why; return false; }
        s.bdev->acquire(job, {&s.shared_colour});
        try {
            TemporalFrame temporal{};
            temporal.reset = frame.reset;
            s.runtime->record_temporal(s.bdev->command_buffer(job), engine.colour, controls, temporal);
        } catch (const std::exception& e) {
            s.bdev->abort(job);
            s.status = std::string("neural rendering failed: ") + e.what();
            s.failed = true;
            log("[nr] %s", s.status.c_str());
            return false;
        }
        if (!s.bdev->end(job, &why)) { s.status = why; return false; }
    } else {
        job = s.record_bridge_job(engine, motion != nullptr, false, controls);
        if (!job) return false;
    }
    const uint64_t gen = s.bdev->generation(job);

    std::vector<const bridge::Image*> images{&s.shared_colour};
    if (motion) images.push_back(&s.shared_motion);
    // Key 0: ours on the D3D10 side. A key that was abandoned (a holder died) is not used again.
    const Keyed took = acquire_all(images, 0, 1000);
    if (took != Keyed::Ok) {
        s.bdev->discard(job, gen);
        s.status = std::string("the shared textures' keyed mutex ") + keyed_text(took);
        if (took != Keyed::Timeout) { s.failed = true; log("[nr] bridge: %s", s.status.c_str()); }
        return false;
    }
    // Our copies must not be skipped by a predicate the game left set.
    ID3D10Predicate* predicate = nullptr;
    BOOL predicate_value = FALSE;
    device->GetPredication(&predicate, &predicate_value);
    if (predicate) device->SetPredication(nullptr, FALSE);
    const auto restore_predicate = [&] {
        if (predicate) { device->SetPredication(predicate, predicate_value); predicate->Release(); predicate = nullptr; }
    };
    device->CopyResource(static_cast<ID3D10Resource*>(s.shared_colour.d3d), target);
    if (motion) device->CopyResource(static_cast<ID3D10Resource*>(s.shared_motion.d3d), motion);
    // Handed to our queue at key 1; a texture that refused stays ours at 0, the rest are taken back to 0.
    std::vector<bool> released;
    if (!release_all(images, 1, &released)) {
        s.bdev->discard(job, gen);
        std::vector<const bridge::Image*> back;
        for (size_t n = 0; n < images.size(); ++n) if (released[n]) back.push_back(images[n]);
        const bool recovered = acquire_all(back, 1, 1000) == Keyed::Ok && release_all(images, 0);
        restore_predicate();
        s.status = "a shared texture's keyed mutex could not be handed over";
        if (!recovered) s.failed = true;
        log("[nr] bridge: %s%s", s.status.c_str(), recovered ? "" : "; the textures are lost to us");
        return false;
    }
    device->Flush();
    if (!s.bdev->submit_keyed(job, gen, images, 1, 2)) {
        // Take the textures back at the key we left them at, so the next frame starts from 0.
        const bool recovered = acquire_all(images, 1, 1000) == Keyed::Ok && release_all(images, 0);
        restore_predicate();
        s.status = "the bridge submit failed";
        if (!recovered) { s.failed = true; log("[nr] bridge: %s; the shared textures are lost to us", s.status.c_str()); }
        return false;
    }
    // Key 2: the network has finished. Waiting here is the only order D3D10 offers (it has no fences).
    const Keyed back = acquire_all(images, 2, 1000);
    if (back != Keyed::Ok) {
        restore_predicate();
        s.status = std::string("the network did not hand the frame back (") + keyed_text(back) + ")";
        s.failed = true;
        log("[nr] bridge: %s", s.status.c_str());
        return false;
    }
    device->CopyResource(target, static_cast<ID3D10Resource*>(s.shared_colour.d3d));
    restore_predicate();
    if (!release_all(images, 0)) {
        s.status = "a shared texture's keyed mutex was not handed back after the frame";
        s.failed = true;
        log("[nr] bridge: %s", s.status.c_str());
    }
    return true;
}

bool Session::Impl::bridge_d3d11(ID3D11Device* device, const D3D11Frame& frame, const Controls& controls) {
    auto& s = *this;
    const LUID luid = adapter_luid(device);
    if (!s.ensure_bridge(luid)) return false;
    ++s.bridge_runs;
    s.sweep_retired_images();
    ID3D11Texture2D* target = nullptr;
    if (FAILED(frame.target->QueryInterface(IID_PPV_ARGS(&target))) || !target) {
        s.status = "the D3D11 frame is not a 2D texture";
        return false;
    }
    D3D11_TEXTURE2D_DESC td{};
    target->GetDesc(&td);
    const DXGI_FORMAT fmt = bridge::shared_format(td.Format);
    const VkFormat vk = fmt == DXGI_FORMAT_UNKNOWN ? VK_FORMAT_UNDEFINED : vulkan_colour_format_of(fmt);
    if (vk == VK_FORMAT_UNDEFINED || td.SampleDesc.Count != 1 || td.MipLevels != 1 || td.ArraySize != 1) {
        target->Release();
        s.status = "the frame's DXGI format " + std::to_string(unsigned(td.Format)) + " is not one the bridge carries";
        return false;
    }
    const bool linear = (frame.upscaler_input || frame.linear_hdr) && !frame.estimate_motion && is_linear_format(vk);
    BridgeRenoDX renodx{};
    if (frame.renodx) {
        if (!frame.motion && Impl::renodx_needs_motion(*frame.renodx)) {
            target->Release();
            s.status = "the RenoDX-style option needs the motion vectors";
            return false;
        }
        D3D11_TEXTURE2D_DESC md{};
        ID3D11Texture2D* m = nullptr;
        if (frame.motion && SUCCEEDED(frame.motion->QueryInterface(IID_PPV_ARGS(&m))) && m) { m->GetDesc(&md); m->Release(); }
        if (!s.bridge_renodx(*frame.renodx, 0, td.Width, td.Height, vk, md.Width, md.Height, controls, &renodx)) {
            target->Release();
            return false;
        }
    } else if (!s.ensure_runtime(controls, td.Width, td.Height, vk, linear)) {
        target->Release();
        return false;
    }
    if (!s.ensure_shared(s.shared_colour, td.Width, td.Height, fmt, nullptr, device, "colour")) {
        target->Release();
        return false;
    }
    ID3D11Resource* motion = nullptr;
    ID3D11Resource* depth = nullptr;
    if (frame.motion) {
        ID3D11Texture2D* m = nullptr;
        D3D11_TEXTURE2D_DESC md{};
        if (SUCCEEDED(frame.motion->QueryInterface(IID_PPV_ARGS(&m))) && m) {
            m->GetDesc(&md);
            const DXGI_FORMAT mf = bridge::shared_format(md.Format);
            if (mf != DXGI_FORMAT_UNKNOWN && md.SampleDesc.Count == 1 && md.MipLevels == 1 && md.ArraySize == 1 &&
                s.ensure_shared(s.shared_motion, md.Width, md.Height, mf, nullptr, device, "motion"))
                motion = m;
            else
                m->Release();
        }
    }
    if (frame.depth) {
        ID3D11Texture2D* d = nullptr;
        D3D11_TEXTURE2D_DESC dd{};
        if (SUCCEEDED(frame.depth->QueryInterface(IID_PPV_ARGS(&d))) && d) {
            d->GetDesc(&dd);
            if (bridge::depth_view_format(dd.Format) != DXGI_FORMAT_UNKNOWN && dd.SampleDesc.Count == 1 &&
                s.ensure_shared(s.shared_depth, dd.Width, dd.Height, DXGI_FORMAT_R32_FLOAT, nullptr, device, "depth"))
                depth = d;
            else
                d->Release();
        }
    }
    if (frame.renodx && !motion && Impl::renodx_needs_motion(*frame.renodx)) {
        if (depth) depth->Release();
        target->Release();
        s.status = "the RenoDX-style option needs the motion vectors, and the bridge cannot carry them";
        return false;
    }
    s.status.clear();
    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    // Our copies and the depth pass must not be skipped by a predicate the game left set; it is put back.
    ID3D11Predicate* predicate = nullptr;
    BOOL predicate_value = FALSE;
    context->GetPredication(&predicate, &predicate_value);
    if (predicate) context->SetPredication(nullptr, FALSE);
    context->CopyResource(static_cast<ID3D11Resource*>(s.shared_colour.d3d), target);
    if (motion) context->CopyResource(static_cast<ID3D11Resource*>(s.shared_motion.d3d), motion);
    if (depth) {
        std::string why;
        if (!s.depth11.run(device, context, depth, static_cast<ID3D11Texture2D*>(s.shared_depth.d3d), &why)) {
            if (!s.depth_warned) { s.depth_warned = true; log("[nr] bridge: running without depth: %s", why.c_str()); }
            depth->Release();
            depth = nullptr;
        }
    }

    EngineFrame engine{};
    engine.colour = bridge_frame(s.shared_colour);
    engine.reset = frame.reset;
    engine.motion_scale_x = frame.motion_scale_x;
    engine.motion_scale_y = frame.motion_scale_y;
    engine.depth_inverted = frame.depth_inverted;
    if (motion) engine.motion = bridge_frame(s.shared_motion);
    if (depth) engine.depth = bridge_frame(s.shared_depth);
    bridge::Job* job = nullptr;
    if (frame.renodx) {
        job = s.record_bridge_job(engine, motion != nullptr, depth != nullptr, controls, &renodx);
    } else if (frame.estimate_motion && !motion) {
        // The Present fallback: the estimator's temporal path, as run_d3d11 records it on DXVK.
        std::string why;
        job = s.begin_job(&why);
        if (!job) s.status = "neural rendering skipped: " + why;
        if (job) {
            s.bdev->acquire(job, {&s.shared_colour});
            try {
                TemporalFrame temporal{};
                temporal.reset = frame.reset;
                s.runtime->record_temporal(s.bdev->command_buffer(job), engine.colour, controls, temporal);
            } catch (const std::exception& e) {
                s.bdev->abort(job);
                job = nullptr;
                s.status = std::string("neural rendering failed: ") + e.what();
                s.failed = true;
                log("[nr] %s", s.status.c_str());
            }
            if (job && !s.bdev->end(job, &why)) { job = nullptr; s.status = why; }
        }
    } else {
        job = s.record_bridge_job(engine, motion != nullptr, depth != nullptr, controls);
    }
    bool ran = false;
    if (job && s.bdev->run_between(context, job, s.bdev->generation(job))) {
        context->CopyResource(target, static_cast<ID3D11Resource*>(s.shared_colour.d3d));
        ran = true;
        static bool first = true;
        if (first) { first = false; log("[nr] bridge: first D3D11 frame handed over through the shared fence"); }
    }
    if (predicate) { context->SetPredication(predicate, predicate_value); predicate->Release(); }
    context->Release();
    if (motion) motion->Release();
    if (depth) depth->Release();
    target->Release();
    return ran;
}

// The RenoDX-style option, shared by every route: the network at the frame's extent on RenoDX's proxy
// (RGBA16F, the DLL's own composition), ready or building in the background.
bool Session::Impl::renodx_runtime(const Controls& controls, uint32_t w, uint32_t h, Controls* c) {
    // The network sees RenoDX's proxy and answers into the stage's own image, one pass: what RenoDX's
    // feature 18 evaluate is. Preprocess runs inside RenoDX's processing (RenoDXFrame::preprocess), not here.
    *c = controls;
    c->preprocess = Preprocess{};
    c->passes = 1;
    // NR's switch is [RenoDX] NeuralUplift (the stage passes the frame through when it is off); the route's own
    // Enabled / Apply model do not apply.
    c->enabled = true;
    c->apply_model = true;
    renodx_native = true;
    const bool ok = ensure_runtime(*c, w, h, VK_FORMAT_R16G16B16A16_SFLOAT, false);
    renodx_native = false;
    return ok && runtime->takes_target(*c);   // the model not applied: the frame stays as it is
}

// The network's extent for this feature's frame (RenoDX's ComputeNrWorkingResolution + CommitNrResolution).
void Session::Impl::renodx_extent(uint64_t feature, uint32_t w, uint32_t h, uint32_t guide_w, uint32_t guide_h,
                                  const renodx::Settings& settings, uint32_t* nw, uint32_t* nh) {
    auto& e = renodx[feature];
    renodx::working_resolution(settings, w, h, guide_w, guide_h, nw, nh);
    const bool settings_changed = e.scale != settings.resolution_scale || e.follow != settings.follow_input_res ||
                                  e.w != w || e.h != h;
    e.scale = settings.resolution_scale; e.follow = settings.follow_input_res;
    if (e.applied_w == 0 || settings_changed || (*nw == e.applied_w && *nh == e.applied_h)) {
        e.applied_w = *nw; e.applied_h = *nh;
        e.pending_frames = 0;
        return;
    }
    // The game's guides changed size: taken only once it has held.
    if (*nw != e.pending_w || *nh != e.pending_h) { e.pending_w = *nw; e.pending_h = *nh; e.pending_frames = 0; }
    if (++e.pending_frames >= renodx::kContractDebounceFrames) {
        e.applied_w = *nw; e.applied_h = *nh;
        e.pending_frames = 0;
        return;
    }
    *nw = e.applied_w; *nh = e.applied_h;
}

// This feature's stage at the frame's extent, the network's, format and HDR judgement (a new one, the old
// retired, when any of them changes), with this frame's settings.
renodx::Stage* Session::Impl::renodx_stage(uint64_t feature, uint32_t w, uint32_t h, uint32_t nw, uint32_t nh,
                                           VkFormat format, const Session::RenoDXFrame& rf) {
    free_retired_stages();
    auto& e = renodx[feature];
    if (!e.stage || e.w != w || e.h != h || e.nw != nw || e.nh != nh || e.format != format || e.hdr != rf.hdr_flag ||
        e.present != rf.present) {
        retire_stage(std::move(e.stage));
        renodx::StageConfig sc;
        sc.shaders = root + "\\dlssnr-amd\\shaders\\renodx";
        sc.width = w; sc.height = h; sc.nr_width = nw; sc.nr_height = nh;
        sc.frame_format = format;
        sc.hdr_flag = rf.hdr_flag;
        sc.present = rf.present;
        sc.settings = rf.settings;   // the units it is made for
        HostDevice host{};
        host.instance = handles.instance; host.physical = handles.physical; host.device = handles.device;
        host.queue = access.queue; host.queue_family = access.family;
        try {
            e.stage = std::make_unique<renodx::Stage>(host, sc);
        } catch (const std::exception& x) {
            status = std::string("RenoDX-style processing unavailable: ") + x.what();
            log("[nr] %s", status.c_str());
            failed = true;
            return nullptr;
        }
        e.w = w; e.h = h; e.nw = nw; e.nh = nh; e.format = format; e.hdr = rf.hdr_flag; e.present = rf.present;
        log("[nr] feature #%u: RenoDX-style processing at %ux%u (network %ux%u), format %d, %s", unsigned(feature), w,
            h, nw, nh, int(format), rf.hdr_flag ? "HDR" : "not HDR");
    }
    try {
        e.stage->set(rf.settings);
    } catch (const std::exception&) {
        // Relative <-> absolute units (NRLinearUnitNits, NRSourceEncoding): a new stage next frame.
        retire_stage(std::move(e.stage));
        return nullptr;
    }
    return e.stage.get();
}

bool Session::Impl::neutral_motion(VkCommandBuffer cmd, uint32_t w, uint32_t h, ColourFrame* out) {
    if (!neutral_mv || neutral_w != w || neutral_h != h) {
        if (neutral_mv) { vkDestroyImage(handles.device, neutral_mv, nullptr); neutral_mv = VK_NULL_HANDLE; }
        if (neutral_mv_memory) { vkFreeMemory(handles.device, neutral_mv_memory, nullptr); neutral_mv_memory = VK_NULL_HANDLE; }
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D; ci.format = VK_FORMAT_R16G16_SFLOAT; ci.extent = {w, h, 1};
        ci.mipLevels = ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_STORAGE_BIT;
        if (vkCreateImage(handles.device, &ci, nullptr, &neutral_mv) != VK_SUCCESS) {
            status = "could not create the neutral motion vectors";
            return false;
        }
        VkMemoryRequirements req{}; vkGetImageMemoryRequirements(handles.device, neutral_mv, &req);
        VkPhysicalDeviceMemoryProperties mem{}; vkGetPhysicalDeviceMemoryProperties(handles.physical, &mem);
        uint32_t type = mem.memoryTypeCount;
        for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
            if ((req.memoryTypeBits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                type = i; break;
            }
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size; ai.memoryTypeIndex = type;
        if (type == mem.memoryTypeCount || vkAllocateMemory(handles.device, &ai, nullptr, &neutral_mv_memory) != VK_SUCCESS ||
            vkBindImageMemory(handles.device, neutral_mv, neutral_mv_memory, 0) != VK_SUCCESS) {
            status = "could not back the neutral motion vectors";
            return false;
        }
        neutral_w = w; neutral_h = h; neutral_cleared = false;
    }
    if (!neutral_cleared) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_GENERAL; b.image = neutral_mv;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &b);
        const VkClearColorValue zero{};
        vkCmdClearColorImage(cmd, neutral_mv, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &b.subresourceRange);
        b.oldLayout = VK_IMAGE_LAYOUT_GENERAL; b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &b);
        neutral_cleared = true;
    }
    *out = {};
    out->image = neutral_mv; out->format = VK_FORMAT_R16G16_SFLOAT; out->width = w; out->height = h;
    out->before = out->after = VK_IMAGE_LAYOUT_GENERAL;
    out->before_stage = out->after_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    out->before_access = out->after_access = VK_ACCESS_SHADER_READ_BIT;
    out->usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    return true;
}

// The stage around the network, `colour` read and `output` written (they may be one image). `engine` carries
// the motion and depth; its colour and target are the stage's.
bool Session::Impl::record_renodx(VkCommandBuffer cmd, renodx::Stage& stage, const ColourFrame& colour,
                                  const ColourFrame& output, EngineFrame engine, const Controls& c,
                                  const Session::RenoDXFrame& rf, const ColourFrame* exposure) {
    // The Present path without the game's motion vectors (NRPresentGuides Optional), or set to Never: neutral guides
    // for the network - zero motion, no depth - and none for the look (its Detail stability is the game's vectors').
    const bool neutral = rf.present && (rf.settings.present_guides == 2u || !engine.motion.image);
    ColourFrame look_motion = engine.motion;
    if (neutral) {
        if (!neutral_motion(cmd, colour.width, colour.height, &engine.motion)) return false;
        engine.motion_x = engine.motion_y = 0;
        engine.motion_texture_width = colour.width; engine.motion_texture_height = colour.height;
        engine.motion_scale_x = engine.motion_scale_y = 1.0f;
        engine.depth = ColourFrame{};
        look_motion = ColourFrame{};
    }
    renodx::StageFrame sf;
    sf.colour = colour;
    sf.output = output;
    sf.reset = engine.reset;
    sf.after_upscale = rf.after_upscale;
    sf.motion = look_motion;
    sf.motion_x = engine.motion_x; sf.motion_y = engine.motion_y;
    sf.motion_scale_x = engine.motion_scale_x; sf.motion_scale_y = engine.motion_scale_y;
    if (exposure) sf.exposure = *exposure;
    sf.pre_exposure = rf.pre_exposure;
    sf.exposure_scale = rf.exposure_scale;
    sf.auto_exposure = rf.auto_exposure;
    sf.create_flags_known = rf.create_flags_known;
    sf.preprocess = rf.preprocess;
    sf.present_white_nits = rf.present_white_nits;
    const renodx::Settings& rs = rf.settings;
    const uint32_t passes = renodx::stack_passes(rs);
    // A shorter stack than last frame: the passes it no longer runs give their histories back (a longer one starts
    // its new passes fresh, as RenoDX's own does).
    // Each pass on a history of its own. The ReShade routes run as feature 0, the runtime's own unnamed history,
    // which it neither saves nor restores around a named one: stacked there, pass 1 takes a name too (else passes 1
    // and 2 would read each other's history every frame).
    const uint64_t named = engine.feature ? engine.feature : kUnnamedStack;
    const auto pass_feature = [&](uint32_t k) { return engine.feature || passes > 1 ? stack_feature(named, k) : 0; };
    auto& entry = renodx[engine.feature];
    for (uint32_t k = passes; k < entry.stack; ++k) release_everywhere(stack_feature(named, k));
    if (!engine.feature && passes == 1 && entry.stack > 1) release_everywhere(named);
    entry.stack = passes;
    // RenoDX's guides: NRDepthMode overrides the game's depth direction, NRMVecScaleX / Y scale its motion vectors
    // (the network's and the look's).
    if (rs.depth_mode == 1u) engine.depth_inverted = false;
    if (rs.depth_mode == 2u) engine.depth_inverted = true;
    engine.motion_scale_x *= rs.mvec_scale_x;
    engine.motion_scale_y *= rs.mvec_scale_y;
    sf.motion_scale_x = engine.motion_scale_x; sf.motion_scale_y = engine.motion_scale_y;
    // Pass k's controls, RenoDX's (the route's own do not apply with the option on): pass 1's, or pass k's once its
    // "Same as pass 1" is off (PassModel; the intensity is always the pass's own, PassIntensity); with
    // NRSkinIndependent and the Character mask on, Structure at most 1 and a stock Skin sent as 1 (SentSkinSteering).
    const auto pass_controls = [&](uint32_t pass) {
        const uint32_t k = std::min(pass, 3u);   // passes 5.. (NRUnlockPasses) take pass 4's controls
        Controls ck = c;
        ck.per_pass.clear();
        ck.passes = 1;
        const bool own = k > 0 && !rs.pass_follow[k - 1];
        ck.intensity = k == 0 ? rs.intensity : rs.pass_intensity[k - 1];
        ck.style = int(own ? rs.pass_style[k - 1] : rs.style);
        ck.local_tone = own ? rs.pass_local_tone[k - 1] : rs.local_tone;
        ck.local_structure = own ? rs.pass_local_structure[k - 1] : rs.local_structure;
        ck.skin_structure = own ? rs.pass_skin_structure[k - 1] : rs.skin_structure;
        ck.automatic_mask = own ? rs.pass_auto_mask[k - 1] : rs.auto_mask;
        if (rs.skin_independent && ck.automatic_mask) {
            ck.local_structure = std::min(ck.local_structure, 1.0f);
            if (ck.skin_structure < 0.0f) ck.skin_structure = 1.0f;
        }
        return ck;
    };
    const bool timed = stage_timing_ready();
    const uint32_t slot = stage_slot, base = slot * kStageQueries;
    uint32_t timed_passes = 0;
    // RenoDX's NR screenshot. The final picture before and after NR: the presented frame on the ReShade route,
    // OptiScaler's finished picture on its route (NR moved there for the capture, nr_pe_optifix.cpp; `clean`: no NR
    // ran on it inline). The model's input and output: the last pass's, after it ran.
    shot_poll();
    std::unique_ptr<Shot> capture;
    const bool final_frame = rf.present != 0 || rf.finished;
    if (!shot && rs.nr_enabled && screenshot::wants_final() && final_frame && rf.clean) {
        if (!screenshot::readable(colour.format)) {
            screenshot::fail("unsupported_present", "the frame's format cannot be read back");
        } else if ((colour.usage && !(colour.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) ||
                   (output.usage && !(output.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT))) {
            screenshot::fail("unsupported_present", "the frame cannot be copied (no transfer use)");
        } else if (!(capture = shot_prepare(colour.width, colour.height, colour.format))) {
            screenshot::abandon("allocation_failed", "the readback buffers could not be allocated");
        } else {
            // HDR stays HDR: PQ codes as they are, scRGB (1.0 = 80 nits) encoded to PQ; SDR as display codes. The
            // Present path knows the swap chain's encoding; OptiScaler's finished picture is the back buffer, where a
            // float format is scRGB (DXGI's only float encoding) and 10 bits are PQ when OptiScaler judged it HDR.
            const bool pq = colour.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
                            colour.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
            const bool linear = colour.format == VK_FORMAT_R16G16B16A16_SFLOAT ||
                                colour.format == VK_FORMAT_R32G32B32A32_SFLOAT ||
                                colour.format == VK_FORMAT_B10G11R11_UFLOAT_PACK32;
            capture->pair.hdr_mode = rf.present ? (rf.present == 2 ? 2 : rf.present == 3 ? 1 : 0)
                                                : linear ? 1 : pq && rf.hdr_flag ? 2 : 0;
            capture->pair.linear_unit_nits = 80.0f;
            capture->pair.meta = std::string("path=") + (rf.present ? "present" : "optiscaler-finished-picture") +
                                 "\ncomparison=final-picture-before-after-nr\nstack=" + std::to_string(passes) +
                                 "\nwidth=" + std::to_string(colour.width) + "\nheight=" + std::to_string(colour.height) +
                                 "\nsource_format=" + std::to_string(int(colour.format)) +
                                 "\nhdr_mode=" + std::to_string(capture->pair.hdr_mode) + "\n";
            shot_copy(cmd, *capture, colour.image, colour.before, colour.before_stage, colour.before_access, 0);
        }
    } else if (screenshot::armed() && !rs.nr_enabled) {
        screenshot::fail("nr_disabled", "enable NR before taking a comparison");
    }
    const bool model_capture = !shot && !capture && rs.nr_enabled && screenshot::wants_model();
    const renodx::Stage::Network network = [&](uint32_t k, const ColourFrame& proxy, const ColourFrame& neural, bool reset) {
        EngineFrame e = engine;
        e.colour = proxy;
        e.target = neural;
        e.reset = reset;
        e.feature = pass_feature(k);
        const bool time_net = timed && timed_passes < renodx::kMaxUnlockedPasses;
        if (time_net) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, stage_timing, base + 2 + 2 * timed_passes);
        runtime->record_engine(cmd, e, pass_controls(k));
        if (time_net) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, stage_timing, base + 3 + 2 * timed_passes++);
        // RenoDX's model diagnostic: the last pass's proxy and network output, sRGB previews (not the displayed frame).
        if (model_capture && k + 1 == passes && !shot && proxy.format == neural.format && proxy.width == neural.width &&
            proxy.height == neural.height && screenshot::readable(proxy.format)) {
            if (auto sh = shot_prepare(proxy.width, proxy.height, proxy.format)) {
                sh->pair.model_diagnostic = true;
                sh->pair.hdr_mode = 0;
                sh->pair.meta = std::string("path=") + (rf.present ? "present" : "optiscaler") +
                                "\ncomparison=model-pass-input-output\ndisplay_reference=0"
                                "\nsource=model-proxy-and-neural-output\npng=rgb8-srgb-clipped-preview"
                                "\nencoding=srgb\nunits=model-proxy-relative"
                                "\nstage=before-look-transfer-and-game-tonemap"
                                "\nexcluded=look,transfer,pedestal,game-upscale,game-tonemap,UI"
                                "\ninput_includes_previous_stack_passes=" + std::string(k ? "1" : "0") +
                                "\nstack_pass=" + std::to_string(k + 1) + "\nstack=" + std::to_string(passes) +
                                "\nwidth=" + std::to_string(proxy.width) + "\nheight=" + std::to_string(proxy.height) +
                                "\nsource_format=" + std::to_string(int(proxy.format)) + "\n";
                shot_copy(cmd, *sh, proxy.image, proxy.after, proxy.after_stage, proxy.after_access, 0);
                shot_copy(cmd, *sh, neural.image, neural.after, neural.after_stage, neural.after_access, sh->bytes);
                shot_recorded(cmd, std::move(sh));
            } else {
                screenshot::abandon("allocation_failed", "model diagnostic readback buffers could not be allocated");
            }
        }
    };
    if (timed) {
        vkCmdResetQueryPool(cmd, stage_timing, base, kStageQueries);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, stage_timing, base);
    }
    try {
        stage.record(cmd, sf, network);
        if (capture) {
            shot_copy(cmd, *capture, output.image, output.after, output.after_stage, output.after_access, capture->bytes);
            shot_recorded(cmd, std::move(capture));
        }
        if (timed) {
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, stage_timing, base + 1);
            stage_slot_passes[slot] = timed_passes;
            ++stage_recorded;
            stage_slot = (slot + 1) % kStageSlots;
            read_stage_timing();
        }
        renodx_timed = timed;
    } catch (const std::exception& x) {
        if (capture) {   // its pre-NR copy may be in the command buffer: kept until the session goes
            screenshot::abandon("failed", "RenoDX-style processing failed during the capture");
            shots_out.push_back(std::move(capture));
        }
        status = std::string("RenoDX-style processing failed: ") + x.what();
        failed = true;
        log("[nr] %s", status.c_str());
        return false;
    }
    return true;
}

bool Session::run_renodx(ID3D12Device* device, ID3D12GraphicsCommandList* list,
                         ID3D12Resource* output, D3D12_RESOURCE_STATES output_state,
                         const EngineResources& resources, const Controls& controls,
                         ID3D12Resource* input, D3D12_RESOURCE_STATES input_state, const RenoDXFrame& rf) {
    auto& s = *impl_;
    if (s.failed || !device || !list || !output || !input) return false;
    if (!resources.motion && Impl::renodx_needs_motion(rf)) return false;
    s.status.clear();
    if (bridged(device)) {
        // Native D3D12: as run_after's bridge path - the list is cut at the evaluate, the input, guides and exposure
        // copied into the shared images, RenoDX's processing and the network run between the halves on our device,
        // the answer copied into the output.
        if (!s.ensure_bridge(device->GetAdapterLuid())) return false;
        BridgeInputs in{};
        in.target = output; in.target_state = output_state;
        if (input != output) { in.source = input; in.source_state = input_state; }
        in.motion = resources.motion; in.motion_state = resources.motion_state;
        in.depth = resources.depth; in.depth_state = resources.depth_state;
        in.motion_subrect = resources.motion_subrect; in.depth_subrect = resources.depth_subrect;
        in.motion_scale_x = resources.motion_scale_x; in.motion_scale_y = resources.motion_scale_y;
        in.depth_inverted = resources.depth_inverted;
        in.reset = resources.reset;
        in.feature = resources.feature;
        in.renodx = &rf;
        in.exposure = rf.exposure; in.exposure_state = rf.exposure_state;
        std::shared_ptr<split::Snapshot> state;
        std::string why;
        const auto before_copy = [&] {
            if (!split::prepare(list, &why)) { s.status = "the command list cannot be cut yet: " + why; return false; }
            state = split::snapshot(list);
            if (!state) { s.status = "the command list's state is not known"; return false; }
            return true;
        };
        bridge::Job* job = s.bridge_prepare_d3d12(device, list, in, controls, before_copy);
        if (!job) return false;
        bridge::Device* d = s.bdev;
        const uint64_t gen = d->generation(job);
        split::Job between;
        between.run = [d, job, gen](ID3D12CommandQueue* queue) { d->run_between(queue, job, gen); };
        between.discard = [d, job, gen] { d->discard(job, gen); };
        if (!split::cut(list, std::move(between), state, &why)) {
            d->discard(job, gen);
            split::restore(list, state);
            s.status = "the command list could not be cut: " + why;
            thread_local std::string said;
            if (said != s.status) { said = s.status; log("[nr] bridge: %s", s.status.c_str()); }
            return false;
        }
        s.record_copy_out(list, output, output_state);
        static bool first = true;
        if (first) {
            first = false;
            log("[nr] bridge: first RenoDX-style frame cut at the evaluate and handed to the network (%ux%u)",
                unsigned(output->GetDesc().Width), output->GetDesc().Height);
        }
        return true;
    }
    if (!s.ensure_d3d12_device(device)) return false;
    const VkCommandBuffer cmd = command_buffer(list);
    if (!cmd) { s.status = "the command list exposes no Vulkan handle"; s.failed = true; return false; }
    const auto target = colour_handle(device, output, output_state);
    if (!target.usable()) { s.status = describe_rejection(target, "output"); return false; }
    const auto source = input == output ? target : colour_handle(device, input, input_state);
    if (!source.usable()) { s.status = describe_rejection(source, "colour"); return false; }
    if (source.format != target.format || source.width != target.width || source.height != target.height) {
        s.status = "RenoDX-style: the colour and the output differ in format or extent";
        return false;
    }
    const uint32_t w = target.width, h = target.height;
    const auto motion_vk = resources.motion ? resource_handle(device, resources.motion, resources.motion_state)
                                            : decltype(resource_handle(device, resources.motion, resources.motion_state)){};
    uint32_t nw = w, nh = h;
    s.renodx_extent(resources.feature, w, h,
                    resources.motion_subrect.width ? resources.motion_subrect.width : motion_vk.width,
                    resources.motion_subrect.height ? resources.motion_subrect.height : motion_vk.height,
                    rf.settings, &nw, &nh);
    Controls c;
    if (!s.renodx_runtime(controls, nw, nh, &c)) return false;
    renodx::Stage* stage = s.renodx_stage(resources.feature, w, h, nw, nh, target.format, rf);
    if (!stage) return false;
    ColourFrame colour{}, out{};
    fill(&colour, source);
    fill(&out, target);
    EngineFrame engine{};
    engine.feature = resources.feature;
    engine.reset = resources.reset;
    engine.motion_scale_x = resources.motion_scale_x;
    engine.motion_scale_y = resources.motion_scale_y;
    engine.depth_inverted = resources.depth_inverted;
    if (resources.motion) {
        if (!fill(&engine.motion, motion_vk)) return false;
        engine.motion_texture_width = engine.motion.width; engine.motion_texture_height = engine.motion.height;
        apply_guide_subrect(&engine.motion, resources.motion_subrect, "motion-vector", &engine.motion_x, &engine.motion_y);
    }
    if (resources.depth &&
        fill(&engine.depth, resource_handle(device, resources.depth, resources.depth_state)))
        apply_guide_subrect(&engine.depth, resources.depth_subrect, "depth", &engine.depth_x, &engine.depth_y);
    // As run_after: full barriers on both sides, vkd3d's tracked compute state forgotten afterwards.
    const auto everything = [&](VkAccessFlags src_access, VkAccessFlags dst_access) {
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask = src_access; mb.dstAccessMask = dst_access;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0, 1, &mb, 0, nullptr, 0, nullptr);
    };
    ColourFrame exposure{};
    const bool have_exposure = rf.exposure && fill(&exposure, resource_handle(device, rf.exposure, rf.exposure_state));
    everything(VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    if (!s.record_renodx(cmd, *stage, colour, out, engine, c, rf, have_exposure ? &exposure : nullptr)) return false;
    everything(VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    list->SetPipelineState(nullptr);
    list->SetComputeRootSignature(nullptr);
    s.forget_bound_pipeline(device, list);
    return true;
}

bool Session::run_present(ID3D12Device* device, ID3D12GraphicsCommandList* list,
                          ID3D12Resource* back_buffer, unsigned width, unsigned height,
                          const Controls& controls) {
    auto& s = *impl_;
    if (s.failed || !device || !list || !back_buffer) return false;
    s.status.clear();
    if (!s.ensure_d3d12_device(device)) return false;
    const VkCommandBuffer cmd = command_buffer(list);
    if (!cmd) { s.status = "the command list exposes no Vulkan handle"; s.failed = true; return false; }
    const auto target = colour_handle(device, back_buffer, D3D12_RESOURCE_STATE_PRESENT);
    if (!target.usable()) { s.status = describe_rejection(target, "back buffer"); return false; }
    if (!s.ensure_runtime(controls, target.width, target.height, target.format, false)) return false;

    // Here the network works on the game's own buffer: there is nothing to hand
    // anyone afterwards, the frame just has to come out enhanced.
    ColourFrame frame{};
    frame.image = target.image;
    frame.format = target.format;
    frame.width = target.width; frame.height = target.height;
    frame.before = frame.after = target.layout;
    try {
        TemporalFrame temporal{};
        s.runtime->record_temporal(cmd, frame, controls, temporal);
    } catch (const std::exception& e) {
        s.status = std::string("neural rendering failed: ") + e.what();
        s.failed = true;
        log("[nr] %s", s.status.c_str());
        return false;
    }
    return true;
}

bool Session::run_d3d11(const D3D11Frame& frame, const Controls& controls) {
    auto& s = *impl_;
    if (s.failed || !frame.device || !frame.target) return false;
    s.status.clear();

    // The native D3D11 runtime: the bridge, on a Vulkan device of our own.
    if (!s.dxvk && s.mode != Impl::Mode::Interop) {
        IDXGIVkInteropDevice* probe = nullptr;
        const bool interop = d3d11_handles(frame.device, &probe).valid();
        if (probe) probe->Release();
        if (s.decide_mode(interop)) {
            ID3D11Device* device = nullptr;
            if (FAILED(frame.device->QueryInterface(IID_PPV_ARGS(&device))) || !device) {
                s.status = "the D3D11 device is not a D3D11 device";
                return false;
            }
            const bool ran = s.bridge_d3d11(device, frame, controls);
            device->Release();
            return ran;
        }
    }
    // DXVK's interop device is the whole of this path: it is where the Vulkan
    // handles, the submission queue and the queue lock all come from.
    if (!s.dxvk) {
        log("[nr] d3d11: asking DXVK for its Vulkan handles");
        s.handles = d3d11_handles(frame.device, &s.dxvk);
        log("[nr] d3d11: instance %p physical %p device %p interop %p",
            static_cast<void*>(s.handles.instance), static_cast<void*>(s.handles.physical),
            static_cast<void*>(s.handles.device), static_cast<void*>(s.dxvk));
        if (!s.handles.valid() || !s.dxvk) {
            if (s.dxvk) { s.dxvk->Release(); s.dxvk = nullptr; }
            s.status = "no Vulkan device underneath; this D3D11 game is not on DXVK";
            s.failed = true;
            log("[nr] %s", s.status.c_str());
            return false;
        }
        if (!d3d11_queue_access(s.dxvk, &s.access)) {
            s.status = "DXVK would not hand out its submission queue";
            s.failed = true;
            log("[nr] %s", s.status.c_str());
            return false;
        }
    }

    // Said once per target, not once per frame: this pair used to be two of the
    // fifteen flushed lines every frame wrote (see first_sight in
    // nr_pe_interop.cpp). What the target is only changes when it changes.
    static VkImage traced_target = VK_NULL_HANDLE;
    const auto target = d3d11_image(frame.target, frame.device);
    if (!target) {
        s.status = "the D3D11 texture exposes no Vulkan image";
        return false;
    }
    if (target.image != traced_target) {
        traced_target = target.image;
        log("[nr] d3d11: queue family %u; target %ux%u format %u layout %u usage 0x%x",
            s.access.family, target.width, target.height, unsigned(target.format),
            unsigned(target.layout), unsigned(target.usage));
    }
    // The pass transfers into and out of whatever image it is given. DXVK tells
    // us what the image may be used for, so this is a check rather than a hope -
    // and a texture without these bits gets declined with a reason instead of
    // producing validation errors inside somebody's game.
    constexpr VkImageUsageFlags required =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((target.usage & required) != required) {
        s.status = "the game's texture cannot be copied to and from; nothing to run on";
        return false;
    }
    Controls renodx_controls{};
    renodx::Stage* renodx_stage = nullptr;
    if (frame.renodx) {
        if (!frame.motion && Impl::renodx_needs_motion(*frame.renodx)) {
            s.status = "the RenoDX-style option needs the motion vectors";
            return false;
        }
        const auto mv = frame.motion ? d3d11_image(frame.motion, frame.device) : decltype(d3d11_image(frame.motion, frame.device)){};
        uint32_t nw = target.width, nh = target.height;
        s.renodx_extent(0, target.width, target.height, mv.width, mv.height, frame.renodx->settings, &nw, &nh);
        if (!s.renodx_runtime(controls, nw, nh, &renodx_controls)) return false;
        if (!(renodx_stage = s.renodx_stage(0, target.width, target.height, nw, nh, target.format, *frame.renodx)))
            return false;
    } else if (!s.ensure_runtime(controls, target.width, target.height, target.format,
                                 (frame.upscaler_input || frame.linear_hdr) && !frame.estimate_motion &&
                                 Session::Impl::is_linear_format(target.format))) return false;
    if (!s.ensure_own_command_buffer()) return false;
    if (!s.wait_for_own_cmd()) return false;

    ColourFrame colour{};
    colour.image = target.image;
    colour.format = target.format;
    colour.width = target.width; colour.height = target.height;
    // Transition from the layout DXVK believes the image is in, and put it back:
    // the game's next use of its own texture must see what it left there.
    colour.before = colour.after = target.layout;
    // What DXVK says the image may be used for: with SAMPLED and STORAGE the runtime reads and
    // writes it in place at Model resolution below 100% instead of copying it out and back.
    colour.usage = target.usage;

    EngineFrame engine{};
    engine.colour = colour;
    engine.reset = frame.reset;
    engine.motion_scale_x = frame.motion_scale_x;
    engine.motion_scale_y = frame.motion_scale_y;
    engine.depth_inverted = frame.depth_inverted;
    const bool motion = frame.motion && fill(&engine.motion, d3d11_image(frame.motion, frame.device));
    if (frame.depth) fill(&engine.depth, d3d11_image(frame.depth, frame.device));

    try {
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkResetCommandBuffer(s.own_cmd, 0);
        vkBeginCommandBuffer(s.own_cmd, &bi);
        if (renodx_stage) {
            if (!motion && Impl::renodx_needs_motion(*frame.renodx)) { vkEndCommandBuffer(s.own_cmd); return false; }
            if (!s.record_renodx(s.own_cmd, *renodx_stage, colour, colour, engine, renodx_controls, *frame.renodx)) {
                vkEndCommandBuffer(s.own_cmd);
                return false;
            }
        } else if (motion) s.runtime->record_engine(s.own_cmd, engine, controls);
        else if (frame.estimate_motion) {
            TemporalFrame temporal{}; temporal.reset = frame.reset;
            s.runtime->record_temporal(s.own_cmd, colour, controls, temporal);
        } else {
            s.runtime->record(s.own_cmd, colour, controls, frame.reset);
        }
        vkEndCommandBuffer(s.own_cmd);
    } catch (const std::exception& e) {
        s.status = std::string("neural rendering failed: ") + e.what();
        s.failed = true;
        log("[nr] %s", s.status.c_str());
        return false;
    }

    // Everything the game has recorded goes in front of us - that is what the
    // flush inside the lock is for - and then our submit happens with the queue
    // held, because DXVK shares one VkQueue with everything else in the process.
    VkResult r;
    {
        QueueAccess::Held held(s.access);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1; si.pCommandBuffers = &s.own_cmd;
        r = vkQueueSubmit(s.access.queue, 1, &si, s.own_fence);
        if (r == VK_SUCCESS) s.own_cmd_pending = true;
    }
    if (r != VK_SUCCESS) {
        s.status = "the submit on the game's queue failed";
        return false;
    }
    return true;
}

bool Session::run_present_vulkan(const DeviceHandles& handles, VkQueue queue, uint32_t family,
                                 VkImage image, VkFormat format, uint32_t width, uint32_t height,
                                 uint32_t slot_index, uint32_t slot_count,
                                 const VkSemaphore* wait, uint32_t wait_count,
                                 VkSemaphore* signalled, bool temporal,
                                 const Controls& controls) {
    auto& s = *impl_;
    if (s.failed || !image || !queue || !handles.valid() || !signalled) return false;
    s.status.clear();
    s.handles = handles;
    s.access.queue = queue;
    s.access.family = family;
    s.access.lock = nullptr;      // this queue is the game's and we are inside its present
    s.access.unlock = nullptr;

    // A game that alternates between two swapchain extents - 007 First Light
    // cycles 3840x2160 and 3840x2096 at startup and on every mode change - would
    // otherwise rebuild the whole network on each one: four gigabytes
    // reallocated, the weights re-uploaded, and the history thrown away. A
    // discarded history closes the temporal gate for that frame, which changes
    // the picture visibly, and doing it every other frame is exactly what a
    // flicker is.
    //
    // So an extent that does not match the built network does not rebuild it: it
    // is counted, the frame is left alone, and the rebuild happens only once the
    // new extent has actually settled. A real resolution change costs half a
    // second of untouched frames; an alternation costs nothing at all.
    if (s.runtime && (s.width != width || s.height != height) && !s.have_runtime(width, height)) {
        if (s.pending_w != width || s.pending_h != height) {
            s.pending_w = width; s.pending_h = height; s.pending_frames = 0;
        }
        if (++s.pending_frames < 30) return false;
        log("[nr] the swapchain has been %ux%u for %u frames; rebuilding from %ux%u",
            width, height, s.pending_frames, s.width, s.height);
    }
    s.pending_frames = 0;

    if (!s.ensure_slots(slot_count)) return false;
    if (slot_index >= s.slots.size()) return false;
    auto& slot = s.slots[slot_index];

    // Every slot, not just this one.
    //
    // The runtime's contract is explicit that the caller serialises uses of its
    // scratch arena, and the per-image slots defeat exactly that: with three
    // swapchain images, three of our command buffers can be executing at once,
    // all reading and writing one activation arena and one history image. The
    // result is frames that alternate between right and wrong, which is what a
    // flickering picture is.
    //
    // So the slots stay - a binary semaphore still needs one per image, and the
    // present depends on that - but only one pass is ever in flight. It costs a
    // pipeline bubble, which is the price of the arena being shared.
    for (auto& other : s.slots) if (!s.wait_slot(other)) return false;
    // Only after the slot is free: rebuilding the runtime destroys images this
    // slot's command buffer may still be reading.
    if (!s.ensure_runtime(controls, width, height, format, false)) return false;

    // A swapchain image at present time is in PRESENT_SRC. The pass transfers in
    // and out of it, and leaves it exactly as it found it: the present that
    // follows is entitled to the layout it was promised.
    ColourFrame colour{};
    colour.image = image;
    colour.format = format;
    colour.width = width; colour.height = height;
    colour.before = colour.after = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    static bool first = true;
    if (first) log("[nr] first fallback frame: %ux%u format %d on queue family %u, image %u of %u",
                   width, height, int(format), family, slot_index, slot_count);
    try {
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkResetCommandBuffer(slot.cmd, 0);
        vkBeginCommandBuffer(slot.cmd, &bi);
        if (temporal) {
            TemporalFrame frame{};
            s.runtime->record_temporal(slot.cmd, colour, controls, frame);
        } else {
            s.runtime->record(slot.cmd, colour, controls);
        }
        vkEndCommandBuffer(slot.cmd);
    } catch (const std::exception& e) {
        s.status = std::string("neural rendering failed: ") + e.what();
        s.failed = true;
        log("[nr] %s", s.status.c_str());
        return false;
    }

    // One wait stage per semaphore, as the spec requires.
    std::vector<VkPipelineStageFlags> stages(wait_count, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = wait_count;
    si.pWaitSemaphores = wait_count ? wait : nullptr;
    si.pWaitDstStageMask = wait_count ? stages.data() : nullptr;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &slot.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &slot.done;
    if (vkQueueSubmit(queue, 1, &si, slot.fence) != VK_SUCCESS) {
        s.status = "the submit before present failed";
        return false;
    }
    slot.pending = true;
    *signalled = slot.done;
    if (first) { first = false; log("[nr] first fallback frame submitted"); }
    return true;
}

void Session::set_vulkan_queue(VkQueue queue, uint32_t family) {
    if (!queue) return;
    impl_->access.queue = queue;
    impl_->access.family = family;
    // No lock: this queue belongs to the game and nothing else in this process
    // shares it through an interop layer, so there is no mutex to take. The
    // caller records into the command buffer the upscaler handed over, which the
    // game submits itself; only the one-off weight upload submits here.
    impl_->access.lock = nullptr;
    impl_->access.unlock = nullptr;
}

VkImageView Session::vulkan_output_view() const { return impl_->vk_output_view; }

VkImage Session::run_vulkan(const DeviceHandles& handles, VkCommandBuffer cmd,
                            const VulkanFrame& frame, const Controls& controls) {
    auto& s = *impl_;
    if (s.failed || !cmd || !frame.colour || !handles.valid()) return VK_NULL_HANDLE;
    s.status.clear();
    s.handles = handles;

    // A Vulkan game gives us no D3D12 resource to hand back, so the output is a
    // plain VkImage of our own rather than one borrowed from a D3D12 allocation -
    // unless the caller takes the answer in its own image (VulkanFrame::in_place).
    Controls renodx_controls{};
    renodx::Stage* renodx_stage = nullptr;
    if (frame.renodx) {
        if (!frame.motion && Impl::renodx_needs_motion(*frame.renodx)) {
            s.status = "the RenoDX-style option needs the motion vectors";
            return VK_NULL_HANDLE;
        }
        uint32_t nw = frame.width, nh = frame.height;
        s.renodx_extent(frame.feature, frame.width, frame.height,
                        frame.motion_subrect.width ? frame.motion_subrect.width : frame.motion_width,
                        frame.motion_subrect.height ? frame.motion_subrect.height : frame.motion_height,
                        frame.renodx->settings, &nw, &nh);
        if (!s.renodx_runtime(controls, nw, nh, &renodx_controls)) return VK_NULL_HANDLE;
        if (!(renodx_stage = s.renodx_stage(frame.feature, frame.width, frame.height, nw, nh, frame.colour_format,
                                            *frame.renodx)))
            return VK_NULL_HANDLE;
    } else {
    if (!frame.in_place && !s.vk_output_valid(frame.width, frame.height, frame.colour_format)) {
        if (!s.ensure_vk_output(frame.width, frame.height, frame.colour_format))
            return VK_NULL_HANDLE;
    }
    if (!s.ensure_runtime(controls, frame.width, frame.height, frame.colour_format,
                          (frame.upscaler_input || frame.linear_hdr) &&
                              Session::Impl::is_linear_format(frame.colour_format)))
        return VK_NULL_HANDLE;
    }

    ColourFrame colour{};
    colour.image = frame.in_place ? frame.colour : s.vk_output;
    colour.format = frame.colour_format;
    colour.width = frame.width; colour.height = frame.height;
    colour.before = colour.after = frame.in_place ? frame.colour_layout : VK_IMAGE_LAYOUT_GENERAL;
    if (frame.in_place) colour.usage = frame.colour_usage;

    EngineFrame engine{};
    engine.colour = colour;
    // The caller's colour read in place and its output written directly (the engine path with
    // the model applied): the copy into vk_output, the copy into the runtime's input and the
    // write-back all go. Anything else keeps the copies below.
    const bool direct = !frame.in_place && frame.output && frame.motion && s.runtime && s.runtime->takes_target(controls) &&
                        (frame.colour_usage & VK_IMAGE_USAGE_SAMPLED_BIT) &&
                        !(std::getenv("NR_VK_COPY") && std::atoi(std::getenv("NR_VK_COPY")));
    if (direct) {
        engine.colour.image = frame.colour;
        engine.colour.before = engine.colour.after = frame.colour_layout;
        engine.colour.usage = frame.colour_usage;
        engine.target = colour;
        engine.target.image = frame.output;
        engine.target.before = engine.target.after = frame.output_layout;
        engine.target.usage = frame.output_usage;
    }
    engine.feature = frame.feature;
    engine.reset = frame.reset;
    engine.motion_scale_x = frame.motion_scale_x;
    engine.motion_scale_y = frame.motion_scale_y;
    engine.depth_inverted = frame.depth_inverted;
    if (frame.motion) {
        engine.motion.image = frame.motion;
        engine.motion.format = frame.motion_format;
        engine.motion.width = frame.motion_width;
        engine.motion.height = frame.motion_height;
        engine.motion.before = engine.motion.after = frame.motion_layout;
        engine.motion.usage = frame.motion_usage;
        engine.motion_texture_width = engine.motion.width; engine.motion_texture_height = engine.motion.height;
        apply_guide_subrect(&engine.motion, frame.motion_subrect, "motion-vector", &engine.motion_x, &engine.motion_y);
    }
    if (frame.depth) {
        engine.depth.image = frame.depth;
        engine.depth.format = frame.depth_format;
        engine.depth.width = frame.depth_width;
        engine.depth.height = frame.depth_height;
        engine.depth.before = engine.depth.after = frame.depth_layout;
        engine.depth.usage = frame.depth_usage;
        apply_guide_subrect(&engine.depth, frame.depth_subrect, "depth", &engine.depth_x, &engine.depth_y);
    }

    if (renodx_stage) {
        ColourFrame in{}, out{};
        in.image = frame.colour; in.format = frame.colour_format; in.width = frame.width; in.height = frame.height;
        in.before = in.after = frame.colour_layout; in.usage = frame.colour_usage;
        const bool to_output = !frame.in_place && frame.output;
        out = in;
        if (to_output) {
            out.image = frame.output; out.before = out.after = frame.output_layout; out.usage = frame.output_usage;
        }
        ColourFrame exposure{};
        const ColourFrame* ex = nullptr;
        if (frame.renodx->vk_exposure) {
            exposure.image = frame.renodx->vk_exposure; exposure.format = frame.renodx->vk_exposure_format;
            exposure.width = frame.renodx->vk_exposure_width; exposure.height = frame.renodx->vk_exposure_height;
            exposure.before = exposure.after = VK_IMAGE_LAYOUT_GENERAL;
            ex = &exposure;
        }
        if (!s.record_renodx(cmd, *renodx_stage, in, out, engine, renodx_controls, *frame.renodx, ex)) return VK_NULL_HANDLE;
        return out.image;
    }
    if (direct) {
        try {
            s.runtime->record_engine(cmd, engine, controls);
        } catch (const std::exception& e) {
            s.status = std::string("neural rendering failed: ") + e.what();
            s.failed = true;
            log("[nr] %s", s.status.c_str());
            return VK_NULL_HANDLE;
        }
        return frame.output;
    }
    if (frame.in_place) {
        try {
            if (frame.motion) s.runtime->record_engine(cmd, engine, controls);
            else s.runtime->record(cmd, colour, controls, engine.reset);
        } catch (const std::exception& e) {
            s.status = std::string("neural rendering failed: ") + e.what();
            s.failed = true;
            log("[nr] %s", s.status.c_str());
            return VK_NULL_HANDLE;
        }
        return frame.colour;
    }
    try {
        VkImageCopy copy{};
        copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.extent = {frame.width, frame.height, 1};
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.oldLayout = frame.colour_layout; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = frame.colour;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
        vkCmdCopyImage(cmd, frame.colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       s.vk_output, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
        std::swap(b.oldLayout, b.newLayout);
        b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
        if (frame.motion) s.runtime->record_engine(cmd, engine, controls);
        else s.runtime->record(cmd, colour, controls, engine.reset);
    } catch (const std::exception& e) {
        s.status = std::string("neural rendering failed: ") + e.what();
        s.failed = true;
        log("[nr] %s", s.status.c_str());
        return VK_NULL_HANDLE;
    }
    return s.vk_output;
}

}  // namespace nr::pe
