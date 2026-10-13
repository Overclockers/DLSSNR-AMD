#pragma once
#include "nr_runtime.hpp"
#include <string>
namespace nr {
class Profile {
  public:
    std::string path, game, error;
    bool external_control_mask = true;
    Profile() = default;
    explicit Profile(const std::string &root);
    bool load(Controls &, std::string &accumulation);
    bool save(const Controls &, const std::string &accumulation);
};
} // namespace nr
