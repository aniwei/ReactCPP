#include "shared/ReactGlobalError.h"

#include <exception>
#include <iostream>
#include <string>

namespace react {

namespace {

void writeErrorMessage(const std::string& message) {
  std::cerr << "React global error: " << message << std::endl;
}

} // namespace

void reportGlobalError(const std::exception& ex) {
  writeErrorMessage(ex.what());
}

void reportGlobalError(const std::string& message) {
  writeErrorMessage(message);
}

void reportGlobalError() {
  writeErrorMessage("Unknown error");
}

} // namespace react
