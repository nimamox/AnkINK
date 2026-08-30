#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ankink {

using OrientationCommandRunner =
    std::function<bool(const std::vector<std::string> &, std::string &)>;

bool apply_kindle_rotation(const std::string &mode,
                           const OrientationCommandRunner &runner,
                           std::string &error);
bool apply_kindle_rotation(const std::string &mode, std::string &error);

} // namespace ankink
