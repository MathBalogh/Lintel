#pragma once
#include "lexer.h"

namespace lintel::parser {

void parse(std::string_view, AST& ast);

} // namespace lintel::parser
