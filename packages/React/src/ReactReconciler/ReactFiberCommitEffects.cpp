#include "ReactReconciler/ReactFiberCommitEffects.h"

#include "ReactReconciler/ReactFiber.h"
#include "ReactReconciler/ReactFiberFlags.h"
#include "ReactReconciler/ReactFiberHookTypes.h"
#include "ReactReconciler/ReactWorkTags.h"
#include "ReactRuntime/ReactRuntime.h"

#include "jsi/jsi.h"

#include <functional>

namespace react {
namespace {

using facebook::jsi::Function;
using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::Value;

bool isFunctionComponentFiber(const FiberNode& fiber) {
  switch (fiber.tag) {
    case WorkTag::FunctionComponent:
    case WorkTag::ForwardRef:
    case WorkTag::SimpleMemoComponent:
      return true;
    default:
      return false;
  }
}

FunctionComponentUpdateQueue* getFunctionComponentUpdateQueue(FiberNode& fiber) {
  return static_cast<FunctionComponentUpdateQueue*>(fiber.updateQueue);
}

void invokeCreate(Runtime& jsRuntime, Effect& effect) {
  Value createValue(jsRuntime, effect.create);
  if (!createValue.isObject()) {
    return;
  }

  Object createObject = createValue.getObject(jsRuntime);
  if (!createObject.isFunction(jsRuntime)) {
    return;
  }

  Function createFn = createObject.asFunction(jsRuntime);
  Value destroy = createFn.call(jsRuntime, nullptr, 0);

  Value instValue(jsRuntime, effect.inst);
  if (!instValue.isObject()) {
    return;
  }

  Object instObject = instValue.getObject(jsRuntime);
  instObject.setProperty(jsRuntime, "destroy", destroy);
}

void invokeDestroy(Runtime& jsRuntime, Effect& effect) {
  Value instValue(jsRuntime, effect.inst);
  if (!instValue.isObject()) {
    return;
  }

  Object instObject = instValue.getObject(jsRuntime);
  Value destroyValue = instObject.getProperty(jsRuntime, "destroy");

  if (destroyValue.isUndefined() || destroyValue.isNull()) {
    instObject.setProperty(jsRuntime, "destroy", Value::undefined());
    return;
  }

  if (!destroyValue.isObject()) {
    instObject.setProperty(jsRuntime, "destroy", Value::undefined());
    return;
  }

  Object destroyObject = destroyValue.getObject(jsRuntime);
  if (!destroyObject.isFunction(jsRuntime)) {
    instObject.setProperty(jsRuntime, "destroy", Value::undefined());
    return;
  }

  Function destroyFn = destroyObject.asFunction(jsRuntime);
  destroyFn.call(jsRuntime, nullptr, 0);
  instObject.setProperty(jsRuntime, "destroy", Value::undefined());
}

void forEachEffect(FiberNode& finishedWork, const std::function<void(Effect&)>& visitor) {
  FunctionComponentUpdateQueue* updateQueue = getFunctionComponentUpdateQueue(finishedWork);
  if (updateQueue == nullptr) {
    return;
  }

  Effect* lastEffect = updateQueue->lastEffect;
  if (lastEffect == nullptr) {
    return;
  }

  Effect* effect = lastEffect->next;
  if (effect == nullptr) {
    return;
  }

  do {
    visitor(*effect);
    effect = effect->next;
  } while (effect != nullptr && effect != lastEffect->next);
}

void commitHookEffectList(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    HookFlags flags,
    FiberNode& finishedWork,
    const std::function<void(Effect&)>& callback) {
  (void)runtime;
  forEachEffect(finishedWork, [&](Effect& effect) {
    if (hasHookFlag(effect.tag, flags)) {
      callback(effect);
    }
  });
}

void commitPassiveUnmountOnFiberImpl(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode& fiber) {
  if (!isFunctionComponentFiber(fiber)) {
    return;
  }

  if ((fiber.flags & Passive) == NoFlags) {
    return;
  }

  commitHookEffectList(
      runtime,
      jsRuntime,
      HookFlags::HasEffect | HookFlags::Passive,
      fiber,
      [&](Effect& effect) {
        (void)runtime;
        invokeDestroy(jsRuntime, effect);
      });
}

void commitPassiveMountOnFiberImpl(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode& fiber) {
  if (!isFunctionComponentFiber(fiber)) {
    return;
  }

  if ((fiber.flags & Passive) == NoFlags) {
    return;
  }

  commitHookEffectList(
      runtime,
      jsRuntime,
      HookFlags::HasEffect | HookFlags::Passive,
      fiber,
      [&](Effect& effect) {
        invokeCreate(jsRuntime, effect);
      });
}

void commitLayoutUnmountOnFiber(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode& fiber) {
  if (!isFunctionComponentFiber(fiber)) {
    return;
  }

  if ((fiber.flags & LayoutMask) == NoFlags) {
    return;
  }

  commitHookEffectList(
      runtime,
      jsRuntime,
      HookFlags::HasEffect | HookFlags::Layout,
      fiber,
      [&](Effect& effect) {
        invokeDestroy(jsRuntime, effect);
      });
  commitHookEffectList(
      runtime,
      jsRuntime,
      HookFlags::HasEffect | HookFlags::Insertion,
      fiber,
      [&](Effect& effect) {
        invokeDestroy(jsRuntime, effect);
      });
}

void commitLayoutMountOnFiber(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode& fiber) {
  if (!isFunctionComponentFiber(fiber)) {
    return;
  }

  if ((fiber.flags & LayoutMask) == NoFlags) {
    return;
  }

  commitHookEffectList(
      runtime,
      jsRuntime,
      HookFlags::HasEffect | HookFlags::Layout,
      fiber,
      [&](Effect& effect) {
        invokeCreate(jsRuntime, effect);
      });
  commitHookEffectList(
      runtime,
      jsRuntime,
      HookFlags::HasEffect | HookFlags::Insertion,
      fiber,
      [&](Effect& effect) {
        invokeCreate(jsRuntime, effect);
      });
}

void traverseFiberChildren(FiberNode& fiber, const std::function<void(FiberNode&)>& visit) {
  for (FiberNode* child = fiber.child; child != nullptr; child = child->sibling) {
    visit(*child);
    traverseFiberChildren(*child, visit);
  }
}

void commitPassiveUnmountTree(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode& root) {
  traverseFiberChildren(root, [&](FiberNode& fiber) {
    commitPassiveUnmountOnFiberImpl(runtime, jsRuntime, fiber);
  });
  commitPassiveUnmountOnFiberImpl(runtime, jsRuntime, root);
}

void commitPassiveMountTree(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode& root) {
  traverseFiberChildren(root, [&](FiberNode& fiber) {
    commitPassiveMountOnFiberImpl(runtime, jsRuntime, fiber);
  });
  commitPassiveMountOnFiberImpl(runtime, jsRuntime, root);
}

void commitLayoutUnmountTree(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode& root) {
  traverseFiberChildren(root, [&](FiberNode& fiber) {
    commitLayoutUnmountOnFiber(runtime, jsRuntime, fiber);
  });
  commitLayoutUnmountOnFiber(runtime, jsRuntime, root);
}

void commitLayoutMountTree(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode& root) {
  traverseFiberChildren(root, [&](FiberNode& fiber) {
    commitLayoutMountOnFiber(runtime, jsRuntime, fiber);
  });
  commitLayoutMountOnFiber(runtime, jsRuntime, root);
}

} // namespace

void commitHookEffectListUnmount(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    HookFlags flags,
    FiberNode& finishedWork,
    FiberNode* /*nearestMountedAncestor*/) {
  commitHookEffectList(runtime, jsRuntime, flags, finishedWork, [&](Effect& effect) {
    invokeDestroy(jsRuntime, effect);
  });
}

void commitHookEffectListMount(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    HookFlags flags,
    FiberNode& finishedWork) {
  commitHookEffectList(runtime, jsRuntime, flags, finishedWork, [&](Effect& effect) {
    invokeCreate(jsRuntime, effect);
  });
}

void commitHookEffects(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode& root) {
  commitPassiveUnmountTree(runtime, jsRuntime, root);
  commitLayoutUnmountTree(runtime, jsRuntime, root);
  commitLayoutMountTree(runtime, jsRuntime, root);
  commitPassiveMountTree(runtime, jsRuntime, root);
}

void commitPassiveUnmountOnFiber(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode& fiber) {
  commitPassiveUnmountOnFiberImpl(runtime, jsRuntime, fiber);
}

void commitPassiveMountOnFiber(
    ReactRuntime& runtime,
    Runtime& jsRuntime,
    FiberNode& fiber) {
  commitPassiveMountOnFiberImpl(runtime, jsRuntime, fiber);
}

} // namespace react
