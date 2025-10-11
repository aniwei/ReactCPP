#pragma once

#include <exception>
#include <string>

namespace react {

void reportGlobalError(const std::exception& ex);
void reportGlobalError(const std::string& message);
void reportGlobalError();

} // namespace react
