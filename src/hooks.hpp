#pragma once

#include "app.hpp"

#include <filesystem>
#include <string>

namespace catlight {

std::string render_hook_script(const Options &options, const std::filesystem::path &exe_path);
std::string install_hook_scripts(const Options &options, const std::filesystem::path &exe_path);
std::string uninstall_hooks(const Options &options);
std::string render_hook_status(const Options &options);

} // namespace catlight
