#pragma once

#include "jsi/jsi.h"

namespace react {

facebook::jsi::Object diffHostProperties(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::Object& prevProps,
    const facebook::jsi::Object& nextProps);

} // namespace react
