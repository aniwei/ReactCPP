#include "ReactReconciler/ReactFiberCallUserSpace.h"

namespace react {

using facebook::jsi::Function;
using facebook::jsi::Runtime;
using facebook::jsi::Value;

Value callLazyInitInDEV(Runtime& runtime, const Function& initFunction, const Value& payload) {
  Value thisValue = Value::undefined();
  Value args[] = {Value(runtime, payload)};
  return initFunction.call(runtime, thisValue, args, 1);
}

} // namespace react
