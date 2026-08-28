#pragma once
#include "../../html/dom.hpp"
#include <string>

std::string dump_doctype(const browser::html::DocumentType *dt, int indent = -1);
std::string dump_node(const browser::html::Node *node, int indent = -1);
std::string dump_dom_document(const std::string &source, browser::html::Document *doc);
