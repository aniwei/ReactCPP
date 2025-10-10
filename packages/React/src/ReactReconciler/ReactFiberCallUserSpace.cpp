#include "ReactReconciler/ReactFiberCallUserSpace.h"

#include <array>

namespace react {

using facebook::jsi::Function;
using facebook::jsi::Runtime;
using facebook::jsi::Value;

Value callLazyInitInDEV(Runtime& runtime, const Function& initFunction, const Value& payload) {
  std::array<Value, 1> args{Value(runtime, payload)};
  const Value* argPtr = args.data();
  return initFunction.call(runtime, argPtr, args.size());
}

} // namespace react
