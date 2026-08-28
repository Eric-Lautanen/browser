#pragma once
#include "tokenizer.hpp"

namespace browser::html {

// H-R1: Split states_tag.cpp 857-line switch — helper per state group
// First step: declarations for tag-name/attr states; full split next batch
void handle_tag_open(Tokenizer& t, char32_t c);
void handle_end_tag_open(Tokenizer& t, char32_t c);
void handle_tag_name(Tokenizer& t, char32_t c);

} // namespace browser::html
