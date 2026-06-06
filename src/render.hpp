#pragma once

#include "app.hpp"

#include <string>
#include <vector>

namespace catlight {

Json snapshot_to_json(const Snapshot &snapshot);
Json doctor_to_json(const std::vector<DoctorCheck> &checks);

std::string render_status_text(const Snapshot &snapshot);
std::string render_waybar_json(const Snapshot &snapshot);
std::string render_doctor_text(const std::vector<DoctorCheck> &checks);
std::string render_help();

} // namespace catlight
