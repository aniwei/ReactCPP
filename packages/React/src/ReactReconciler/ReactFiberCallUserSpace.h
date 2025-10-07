#pragma once

#include "jsi/jsi.h"

namespace react {

facebook::jsi::Value callLazyInitInDEV(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Function& initFunction,
    const facebook::jsi::Value& payload);

} // namespace react
