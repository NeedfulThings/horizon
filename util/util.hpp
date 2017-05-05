#pragma once
#include "json.hpp"
#include "common.hpp"
#include <string>

namespace horizon {
	using json = nlohmann::json;
	void save_json_to_file(const std::string &filename, const json &j);
	int orientation_to_angle(Orientation o);
	std::string get_exe_dir();
	std::string coord_to_string(const Coordf &c, bool delta=false);
	std::string dim_to_string(int64_t x);
}
