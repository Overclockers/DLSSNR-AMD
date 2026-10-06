// Native Windows entry point for the existing full-network diagnostic runner.
// Keep graph construction, timing and output checks in the original runner.
#define main nr_graph_main
#include "../src/core/nr_graph.cpp"
#undef main
#include "../src/core/nr_native_plan.hpp"

int main(int argc, char** argv) try {
    if (argc < 4) {
        std::fprintf(stderr, "usage: nr_benchmark WIDTH HEIGHT PLAN_FILE [nr_graph options]\n");
        return 1;
    }
    auto dimension = [](const char* text) {
        char* end = nullptr;
        const long n = std::strtol(text, &end, 10);
        if (end == text || *end || n < 1 || n > 16384)
            throw std::invalid_argument("dimensions must be integers in 1..16384");
        return uint32_t(n);
    };
    const auto plan = nr::make_native_plan(dimension(argv[1]), dimension(argv[2]));
    std::ofstream out(argv[3]);
    out << plan.text;
    out.close();
    if (!out) throw std::runtime_error("cannot write native plan");
    std::vector<std::string> args{argv[0], "--plan", argv[3],
        "--source-width", argv[1], "--source-height", argv[2]};
    for (int i = 4; i < argc; ++i) args.emplace_back(argv[i]);
    std::vector<char*> pointers;
    for (auto& a : args) pointers.push_back(a.data());
    return nr_graph_main(int(pointers.size()), pointers.data());
} catch (const std::exception& e) {
    std::fprintf(stderr, "nr_benchmark: %s\n", e.what());
    return 1;
}
