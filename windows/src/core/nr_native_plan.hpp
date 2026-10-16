#pragma once
#include <cstdint>
#include <string>
namespace nr {
enum class Model { HT, CCSmallFP8 };
// Identify the graph from the model index, so changing weights also selects its topology.
Model model_from_pack(const std::string& path);
struct NativePlan {
    uint32_t width{}, height{}, reductions_x{}, reductions_y{};
    std::string text, model;
};
// Source and returned working dimensions are raster dimensions. Plan scalars
// preserve the recovered host order (height, width); pixels are never rotated.
NativePlan make_native_plan(uint32_t source_width, uint32_t source_height, Model model = Model::HT);
}
