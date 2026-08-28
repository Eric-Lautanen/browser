#pragma once
#include "../../render/paint.hpp"
#include <string>
std::string dump_command(const browser::render::PaintCommand &cmd);
std::string dump_display_list(const browser::render::DisplayList *list);
