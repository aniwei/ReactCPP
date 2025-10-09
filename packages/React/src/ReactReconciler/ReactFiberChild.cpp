#include "ReactReconciler/ReactFiberChild.h"

#include "ReactReconciler/ReactFiber.h"
#include "ReactReconciler/ReactFiberFlags.h"
#include "ReactReconciler/ReactFiberHydrationContext.h"
#include "ReactReconciler/ReactFiberNewContext.h"
#include "ReactReconciler/ReactFiberThenable.h"
#include "ReactReconciler/ReactFiberTreeContext.h"
#include "ReactReconciler/ReactTypeOfMode.h"
#include "ReactReconciler/ReactWorkTags.h"
#include "shared/ReactSymbols.h"
#include "ReactRuntime/ReactJSXRuntime.h"

#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace react {

namespace {

using facebook::jsi::Array;
using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Symbol;
using facebook::jsi::Value;

std::unique_ptr<ThenableState> currentThenableState;
std::size_t thenableIndexCounter = 0;
thread_local ReactRuntime* currentReactRuntime = nullptr;

class RuntimeScope {
 public:
  explicit RuntimeScope(ReactRuntime* runtime) : previous(currentReactRuntime) {
    currentReactRuntime = runtime;
  }

  ~RuntimeScope() {
    currentReactRuntime = previous;
  }

 private:
  ReactRuntime* previous;
};

class ThenableScope {
 public:
  ThenableScope()
      : previousState(std::move(currentThenableState)), previousIndex(thenableIndexCounter) {
    thenableIndexCounter = 0;
  }

  ~ThenableScope() {
    currentThenableState = std::move(previousState);
    thenableIndexCounter = previousIndex;
  }

 private:
  std::unique_ptr<ThenableState> previousState;
  std::size_t previousIndex{0};
};

ReactRuntime* getCurrentReactRuntime() {
  return currentReactRuntime;
}

void recordChildForkIfHydrating(FiberNode& returnFiber, std::size_t forkCount) {
  if (forkCount == 0) {
    return;
  }
  ReactRuntime* runtime = getCurrentReactRuntime();
  if (runtime != nullptr && getIsHydrating(*runtime)) {
    pushTreeFork(returnFiber, forkCount);
  }
}

Value* storeValue(Runtime& runtime, const Value& source) {
  return new Value(runtime, source);
}

ThenableState& ensureThenableState(Runtime& runtime) {
  if (currentThenableState == nullptr) {
    currentThenableState = std::make_unique<ThenableState>(createThenableState(runtime));
  }
  return *currentThenableState;
}

bool isThenable(Runtime& runtime, const Value& value) {
  if (!value.isObject()) {
    return false;
  }
  Object objectValue = value.getObject(runtime);
  if (!objectValue.hasProperty(runtime, "then")) {
    return false;
  }
  Value thenValue = objectValue.getProperty(runtime, "then");
  if (!thenValue.isObject()) {
    return false;
  }
  Object thenObject = thenValue.getObject(runtime);
  return thenObject.isFunction(runtime);
}

Value unwrapThenable(Runtime& runtime, const Value& thenableValue) {
  ThenableState& state = ensureThenableState(runtime);
  std::size_t index = thenableIndexCounter;
  thenableIndexCounter += 1;
  Value resolved(runtime, trackUsedThenable(runtime, state, thenableValue, index));
  return resolved;
}

bool isNullLike(const Value& value) {
  if (value.isNull() || value.isUndefined()) {
    return true;
  }
  if (value.isBool() && !value.getBool()) {
    return true;
  }
  return false;
}

bool isTextLike(const Value& value) {
  return value.isString() || value.isNumber();
}

std::string valueToKey(Runtime& runtime, const Value& value) {
  if (value.isString()) {
    return value.getString(runtime).utf8(runtime);
  }
  if (value.isNumber()) {
    std::ostringstream out;
    out << value.getNumber();
    return out.str();
  }
  return std::string{};
}

std::string valueToText(Runtime& runtime, const Value& value) {
  if (value.isString()) {
    return value.getString(runtime).utf8(runtime);
  }
  if (value.isNumber()) {
    std::ostringstream out;
    out << value.getNumber();
    return out.str();
  }
  return std::string{};
}

bool isSymbol(Runtime& runtime, const Value& value, const ReactSymbolDescriptor& descriptor) {
  if (!value.isSymbol()) {
    return false;
  }
  Symbol descriptorSymbol = resolveSymbol(runtime, descriptor);
  return value.getSymbol(runtime).strictEquals(runtime, descriptorSymbol);
}

bool toBoolean(Runtime& runtime, const Value& value) {
  if (value.isBool()) {
    return value.getBool();
  }
  if (value.isNull() || value.isUndefined()) {
    return false;
  }
  if (value.isNumber()) {
    return value.getNumber() != 0;
  }
  if (value.isString()) {
    return !value.getString(runtime).utf8(runtime).empty();
  }
  if (value.isObject()) {
    return true;
  }
  return false;
}

WorkTag resolveTagForElement(Runtime& runtime, const Value& typeValue) {
  if (typeValue.isString()) {
    return WorkTag::HostComponent;
  }

  if (isSymbol(runtime, typeValue, REACT_FRAGMENT_TYPE)) {
    return WorkTag::Fragment;
  }
  if (isSymbol(runtime, typeValue, REACT_PROFILER_TYPE)) {
    return WorkTag::Profiler;
  }
  if (isSymbol(runtime, typeValue, REACT_STRICT_MODE_TYPE)) {
    return WorkTag::Mode;
  }
  if (isSymbol(runtime, typeValue, REACT_SUSPENSE_TYPE)) {
    return WorkTag::SuspenseComponent;
  }
  if (isSymbol(runtime, typeValue, REACT_SUSPENSE_LIST_TYPE)) {
    return WorkTag::SuspenseListComponent;
  }
  if (isSymbol(runtime, typeValue, REACT_LAZY_TYPE)) {
    return WorkTag::LazyComponent;
  }
  if (isSymbol(runtime, typeValue, REACT_MEMO_TYPE)) {
    return WorkTag::MemoComponent;
  }
  if (isSymbol(runtime, typeValue, REACT_FORWARD_REF_TYPE)) {
    return WorkTag::ForwardRef;
  }

  return WorkTag::FunctionComponent;
}

FiberNode* createFiberFromReactElement(
    Runtime& runtime,
    FiberNode& returnFiber,
    const jsx::ReactElement& element,
    Lanes lanes) {
  const Value& typeValue = element.type;
  WorkTag tag = resolveTagForElement(runtime, typeValue);

  std::string key;
  if (element.key.has_value()) {
    key = valueToKey(runtime, element.key.value());
  }

  const Value propsValue(runtime, element.props);
  Value* propsStorage = storeValue(runtime, propsValue);

  FiberNode* fiber = createFiber(tag, propsStorage, key, returnFiber.mode);
  fiber->lanes = lanes;

  Value* typeStorage = storeValue(runtime, typeValue);
  fiber->type = typeStorage;
  fiber->elementType = typeStorage;

  if (element.ref.has_value()) {
    fiber->ref = storeValue(runtime, element.ref.value());
  } else {
    fiber->ref = nullptr;
  }

  return fiber;
}

FiberNode* createTextFiber(Runtime& runtime, FiberNode& returnFiber, const Value& value, Lanes lanes) {
  const std::string textContent = valueToText(runtime, value);
  const String textString = String::createFromUtf8(runtime, textContent);
  Value textValue(runtime, textString);
  Value* textStorage = storeValue(runtime, textValue);

  FiberNode* fiber = createFiber(WorkTag::HostText, textStorage, std::string{}, returnFiber.mode);
  fiber->lanes = lanes;
  return fiber;
}

struct PortalState {
  Value* containerInfo{nullptr};
  Value* pendingChildren{nullptr};
  Value* implementation{nullptr};
};

void* getPortalContainerInfoImpl(FiberNode& fiber) {
  if (fiber.tag != WorkTag::HostPortal) {
    return nullptr;
  }

  auto* state = static_cast<PortalState*>(fiber.stateNode);
  if (state == nullptr) {
    return nullptr;
  }

  return state->containerInfo;
}

constexpr const char* kTypeofProp = "$$typeof";
constexpr const char* kChildrenProp = "children";
constexpr const char* kKeyProp = "key";
constexpr const char* kContainerInfoProp = "containerInfo";
constexpr const char* kImplementationProp = "implementation";

Value normalizePortalChildren(Runtime& runtime, const Object& portalObject) {
  Value childrenValue = portalObject.getProperty(runtime, kChildrenProp);
  if (childrenValue.isUndefined() || childrenValue.isNull()) {
    Array empty(runtime, 0);
    return Value(runtime, empty);
  }
  return Value(runtime, childrenValue);
}

Value* storeOptionalValue(Runtime& runtime, const Value& value) {
  return storeValue(runtime, value);
}

PortalState* createPortalState(Runtime& runtime, const Object& portalObject) {
  auto* state = new PortalState();
  Value containerValue = portalObject.getProperty(runtime, kContainerInfoProp);
  state->containerInfo = storeOptionalValue(runtime, containerValue);
  state->pendingChildren = nullptr;
  Value implementationValue = portalObject.getProperty(runtime, kImplementationProp);
  state->implementation = storeOptionalValue(runtime, implementationValue);
  return state;
}

std::string portalKeyFromObject(Runtime& runtime, const Object& portalObject) {
  Value keyValue = portalObject.getProperty(runtime, kKeyProp);
  if (keyValue.isUndefined() || keyValue.isNull()) {
    return std::string{};
  }
  return valueToKey(runtime, keyValue);
}

bool isReactPortalObject(Runtime& runtime, const Object& objectValue) {
  Value typeofValue = objectValue.getProperty(runtime, kTypeofProp);
  if (!typeofValue.isSymbol()) {
    return false;
  }
  return isSymbol(runtime, typeofValue, REACT_PORTAL_TYPE);
}

bool isReactPortalValue(Runtime& runtime, const Value& value) {
  if (!value.isObject()) {
    return false;
  }
  Object objectValue = value.getObject(runtime);
  return isReactPortalObject(runtime, objectValue);
}

bool portalStateMatches(Runtime& runtime, const FiberNode& fiber, const Object& portalObject) {
  if (fiber.tag != WorkTag::HostPortal) {
    return false;
  }

  auto* state = static_cast<PortalState*>(fiber.stateNode);
  if (state == nullptr || state->containerInfo == nullptr || state->implementation == nullptr) {
    return false;
  }

  Value containerValue = portalObject.getProperty(runtime, kContainerInfoProp);
  if (!state->containerInfo->strictEquals(runtime, containerValue)) {
    return false;
  }

  Value implementationValue = portalObject.getProperty(runtime, kImplementationProp);
  if (!state->implementation->strictEquals(runtime, implementationValue)) {
    return false;
  }

  return true;
}

FiberNode* createPortalFiber(
    Runtime& runtime,
    FiberNode& returnFiber,
    Value* pendingProps,
    const Object& portalObject,
    std::string key,
    Lanes lanes) {
  FiberNode* fiber = createFiber(WorkTag::HostPortal, pendingProps, std::move(key), returnFiber.mode);
  fiber->lanes = lanes;
  fiber->stateNode = createPortalState(runtime, portalObject);
  return fiber;
}

FiberNode* createFragmentFiber(
    FiberNode& returnFiber,
    Value* children,
    Lanes lanes,
    std::string key = std::string{}) {
  FiberNode* fiber = createFiber(WorkTag::Fragment, children, std::move(key), returnFiber.mode);
  fiber->lanes = lanes;
  return fiber;
}

Array collectValuesFromIterator(Runtime& runtime, const Value& iterableValue, const Value& iteratorFnValue) {
  if (!iterableValue.isObject()) {
    throw std::invalid_argument("Iterator source must be an object");
  }

  Object iterableObject = iterableValue.getObject(runtime);
  if (!iteratorFnValue.isObject()) {
    throw std::invalid_argument("Iterator function must be an object");
  }

  Object iteratorFnObject = iteratorFnValue.getObject(runtime);
  if (!iteratorFnObject.isFunction(runtime)) {
    throw std::invalid_argument("Iterator function must be callable");
  }

  auto iteratorFn = iteratorFnObject.asFunction(runtime);
  Value iteratorValue = iteratorFn.callWithThis(runtime, iterableValue, nullptr, 0);
  if (!iteratorValue.isObject()) {
    throw std::invalid_argument("Iterator call did not return an object");
  }

  Object iteratorObject = iteratorValue.getObject(runtime);
  Value nextValue = iteratorObject.getProperty(runtime, "next");
  if (!nextValue.isObject()) {
    throw std::invalid_argument("Iterator.next is not a function");
  }

  Object nextObject = nextValue.getObject(runtime);
  if (!nextObject.isFunction(runtime)) {
    throw std::invalid_argument("Iterator.next is not callable");
  }

  auto nextFn = nextObject.asFunction(runtime);
  std::vector<Value> collected;

  while (true) {
    Value iteratorThis(runtime, iteratorObject);
    Value resultValue = nextFn.callWithThis(runtime, iteratorThis, nullptr, 0);
    if (!resultValue.isObject()) {
      throw std::invalid_argument("Iterator result is not an object");
    }

    Object resultObject = resultValue.getObject(runtime);
    Value doneValue = resultObject.getProperty(runtime, "done");
    const bool isDone = toBoolean(runtime, doneValue);
    if (isDone) {
      break;
    }

    Value valueValue = resultObject.getProperty(runtime, "value");
    collected.emplace_back(Value(runtime, valueValue));
  }

  Array array(runtime, collected.size());
  for (size_t index = 0; index < collected.size(); ++index) {
    array.setValueAtIndex(runtime, index, collected[index]);
  }

  return array;
}

void deleteChild(FiberNode& returnFiber, FiberNode* childToDelete, bool shouldTrackSideEffects) {
  if (!shouldTrackSideEffects || childToDelete == nullptr) {
    return;
  }
  returnFiber.deletions.push_back(childToDelete);
  returnFiber.flags = static_cast<FiberFlags>(returnFiber.flags | ChildDeletion);
}

void deleteRemainingChildren(
    FiberNode& returnFiber,
    FiberNode* currentFirstChild,
    bool shouldTrackSideEffects) {
  while (currentFirstChild != nullptr) {
    FiberNode* next = currentFirstChild->sibling;
    deleteChild(returnFiber, currentFirstChild, shouldTrackSideEffects);
    currentFirstChild = next;
  }
}

FiberNode* placeSingleChild(FiberNode& returnFiber, FiberNode* child, bool shouldTrackSideEffects) {
  if (child == nullptr) {
    return nullptr;
  }
  child->returnFiber = &returnFiber;
  if (shouldTrackSideEffects && child->alternate == nullptr) {
    child->flags = static_cast<FiberFlags>(child->flags | Placement | PlacementDEV);
  }
  child->sibling = nullptr;
  return child;
}

std::string makeIndexKey(std::size_t index) {
  return "#" + std::to_string(index);
}

std::string fiberMapKey(const FiberNode& fiber) {
  if (!fiber.key.empty()) {
    return fiber.key;
  }
  return makeIndexKey(fiber.index);
}

std::string childMapKey(Runtime& runtime, const Value& childValue, std::size_t index) {
  if (jsx::isReactElementValue(runtime, childValue)) {
    auto element = jsx::getReactElementFromValue(runtime, childValue);
    if (element->key.has_value()) {
      std::string key = valueToKey(runtime, element->key.value());
      if (!key.empty()) {
        return key;
      }
    }
  }
 
  if (isReactPortalValue(runtime, childValue)) {
    Object portalObject = childValue.getObject(runtime);
    std::string key = portalKeyFromObject(runtime, portalObject);
    if (!key.empty()) {
      return key;
    }
  }
 
  return makeIndexKey(index);
}

int placeChildWithTracking(
    FiberNode& returnFiber,
    FiberNode* child,
    int lastPlacedIndex,
    std::size_t newIndex,
    bool shouldTrackSideEffects) {
  if (child == nullptr) {
    return lastPlacedIndex;
  }

  child->index = static_cast<std::uint32_t>(newIndex);
  child->returnFiber = &returnFiber;
  child->sibling = nullptr;

  if (!shouldTrackSideEffects) {
    child->flags = static_cast<FiberFlags>(child->flags | Forked);
    return lastPlacedIndex;
  }

  FiberNode* const current = child->alternate;
  if (current != nullptr) {
    const int oldIndex = static_cast<int>(current->index);
    if (oldIndex < lastPlacedIndex) {
      child->flags = static_cast<FiberFlags>(child->flags | Placement | PlacementDEV);
      return lastPlacedIndex;
    }
    return oldIndex;
  }

  child->flags = static_cast<FiberFlags>(child->flags | Placement | PlacementDEV);
  return lastPlacedIndex;
}

FiberNode* reconcileSingleTextNode(
    Runtime& runtime,
    FiberNode* currentFirstChild,
    FiberNode& workInProgress,
    const Value& nextChild,
    Lanes renderLanes,
    bool shouldTrackSideEffects) {
  const std::string textContent = valueToText(runtime, nextChild);
  const String textString = String::createFromUtf8(runtime, textContent);
  Value textValue(runtime, textString);
  Value* textStorage = storeValue(runtime, textValue);

  if (currentFirstChild != nullptr && currentFirstChild->tag == WorkTag::HostText) {
    if (shouldTrackSideEffects) {
      deleteRemainingChildren(workInProgress, currentFirstChild->sibling, shouldTrackSideEffects);
    }
    FiberNode* existing = createWorkInProgress(currentFirstChild, textStorage);
    return placeSingleChild(workInProgress, existing, shouldTrackSideEffects);
  }

  if (shouldTrackSideEffects) {
    deleteRemainingChildren(workInProgress, currentFirstChild, shouldTrackSideEffects);
  }

  FiberNode* created = createTextFiber(runtime, workInProgress, nextChild, renderLanes);
  return placeSingleChild(workInProgress, created, shouldTrackSideEffects);
}

bool fiberTypeMatchesElement(Runtime& runtime, const FiberNode& fiber, const jsx::ReactElement& element, WorkTag expectedTag) {
  if (fiber.tag != expectedTag) {
    return false;
  }

  if (expectedTag == WorkTag::Fragment) {
    return true;
  }

  if (fiber.elementType != nullptr) {
    const auto* fiberElementType = static_cast<const Value*>(fiber.elementType);
    if (fiberElementType != nullptr && fiberElementType->strictEquals(runtime, element.type)) {
      return true;
    }
  }

  auto* fiberTypeValue = static_cast<Value*>(fiber.type);
  if (fiberTypeValue != nullptr && fiberTypeValue->strictEquals(runtime, element.type)) {
    return true;
  }

  if (expectedTag == WorkTag::LazyComponent && element.type.isObject()) {
    Value resolvedType = resolveLazy(runtime, element.type);
    if (fiberTypeValue != nullptr && fiberTypeValue->strictEquals(runtime, resolvedType)) {
      return true;
    }
  }

  return false;
}

FiberNode* createFiberForPortalValue(
    Runtime& runtime,
    FiberNode& returnFiber,
    FiberNode* existing,
    const Object& portalObject,
    Lanes renderLanes,
    bool& didReuseExisting) {
  didReuseExisting = false;

  Value normalizedChildren = normalizePortalChildren(runtime, portalObject);
  Value* childrenStorage = storeValue(runtime, normalizedChildren);

  if (existing != nullptr && portalStateMatches(runtime, *existing, portalObject)) {
    FiberNode* clone = createWorkInProgress(existing, childrenStorage);
    didReuseExisting = true;
    return clone;
  }

  std::string key = portalKeyFromObject(runtime, portalObject);
  return createPortalFiber(runtime, returnFiber, childrenStorage, portalObject, std::move(key), renderLanes);
}

FiberNode* reconcileSingleElement(
    Runtime& runtime,
    FiberNode* currentFirstChild,
    FiberNode& workInProgress,
    const jsx::ReactElement& element,
    Lanes renderLanes,
    bool shouldTrackSideEffects) {
  std::string key;
  if (element.key.has_value()) {
    key = valueToKey(runtime, element.key.value());
  }

  if (currentFirstChild != nullptr) {
    const bool keysMatch = currentFirstChild->key == key;
    const WorkTag expectedTag = resolveTagForElement(runtime, element.type);
    if (keysMatch && fiberTypeMatchesElement(runtime, *currentFirstChild, element, expectedTag)) {
      if (shouldTrackSideEffects) {
        deleteRemainingChildren(workInProgress, currentFirstChild->sibling, shouldTrackSideEffects);
      }
      Value* propsStorage = storeValue(runtime, element.props);
      FiberNode* existing = createWorkInProgress(currentFirstChild, propsStorage);
      if (element.ref.has_value()) {
        existing->ref = storeValue(runtime, element.ref.value());
      } else {
        existing->ref = nullptr;
      }
      return placeSingleChild(workInProgress, existing, shouldTrackSideEffects);
    }

    if (shouldTrackSideEffects) {
      deleteRemainingChildren(workInProgress, currentFirstChild, shouldTrackSideEffects);
    }
  }

  FiberNode* created = createFiberFromReactElement(runtime, workInProgress, element, renderLanes);
  return placeSingleChild(workInProgress, created, shouldTrackSideEffects);
}

FiberNode* reconcileSinglePortal(
    Runtime& runtime,
    FiberNode* currentFirstChild,
    FiberNode& workInProgress,
    const Object& portalObject,
    Lanes renderLanes,
    bool shouldTrackSideEffects) {
  std::string key = portalKeyFromObject(runtime, portalObject);

  FiberNode* child = currentFirstChild;
  while (child != nullptr) {
    if (child->key == key) {
      if (portalStateMatches(runtime, *child, portalObject)) {
        if (shouldTrackSideEffects) {
          deleteRemainingChildren(workInProgress, child->sibling, shouldTrackSideEffects);
        }
        bool didReuseExisting = false;
        FiberNode* existing = createFiberForPortalValue(
            runtime, workInProgress, child, portalObject, renderLanes, didReuseExisting);
        if (!didReuseExisting && shouldTrackSideEffects) {
          deleteChild(workInProgress, child, shouldTrackSideEffects);
        }
        return placeSingleChild(workInProgress, existing, shouldTrackSideEffects);
      }

      if (shouldTrackSideEffects) {
        deleteRemainingChildren(workInProgress, child, shouldTrackSideEffects);
      }
      break;
    }

    if (shouldTrackSideEffects) {
      deleteChild(workInProgress, child, shouldTrackSideEffects);
    }

    child = child->sibling;
  }

  bool didReuseExisting = false;
  FiberNode* created = createFiberForPortalValue(
      runtime, workInProgress, nullptr, portalObject, renderLanes, didReuseExisting);
  return placeSingleChild(workInProgress, created, shouldTrackSideEffects);
}

FiberNode* createFiberForChildValue(
    Runtime& runtime,
    FiberNode& returnFiber,
    FiberNode* existing,
    const Value& childValue,
    Lanes renderLanes,
    bool& didReuseExisting) {
  didReuseExisting = false;

  if (isNullLike(childValue)) {
    return nullptr;
  }

  if (isTextLike(childValue)) {
    const std::string textContent = valueToText(runtime, childValue);
    const String textString = String::createFromUtf8(runtime, textContent);
    Value textValue(runtime, textString);
    Value* textStorage = storeValue(runtime, textValue);

    if (existing != nullptr && existing->tag == WorkTag::HostText) {
      FiberNode* clone = createWorkInProgress(existing, textStorage);
      didReuseExisting = true;
      return clone;
    }

    return createTextFiber(runtime, returnFiber, childValue, renderLanes);
  }

  if (jsx::isReactElementValue(runtime, childValue)) {
    auto element = jsx::getReactElementFromValue(runtime, childValue);
    const WorkTag expectedTag = resolveTagForElement(runtime, element->type);
    if (existing != nullptr && fiberTypeMatchesElement(runtime, *existing, *element, expectedTag)) {
      Value* propsStorage = storeValue(runtime, element->props);
      FiberNode* clone = createWorkInProgress(existing, propsStorage);
      if (element->ref.has_value()) {
        clone->ref = storeValue(runtime, element->ref.value());
      } else {
        clone->ref = nullptr;
      }
      didReuseExisting = true;
      return clone;
    }

    return createFiberFromReactElement(runtime, returnFiber, *element, renderLanes);
  }

  if (childValue.isObject()) {
    const Object objectValue = childValue.getObject(runtime);

    Value typeofValue = objectValue.getProperty(runtime, kTypeofProp);
    if (typeofValue.isSymbol()) {
      if (isSymbol(runtime, typeofValue, REACT_LAZY_TYPE)) {
        Value resolvedValue = resolveLazy(runtime, childValue);
        return createFiberForChildValue(
            runtime, returnFiber, existing, resolvedValue, renderLanes, didReuseExisting);
      }

      if (isSymbol(runtime, typeofValue, REACT_CONTEXT_TYPE)) {
        Value resolvedValue = readContextDuringReconciliation(runtime, returnFiber, childValue, renderLanes);
        return createFiberForChildValue(
            runtime, returnFiber, existing, resolvedValue, renderLanes, didReuseExisting);
      }
    }

    if (objectValue.isArray(runtime)) {
      Value* childrenStorage = storeValue(runtime, childValue);

      if (existing != nullptr && existing->tag == WorkTag::Fragment) {
        FiberNode* clone = createWorkInProgress(existing, childrenStorage);
        didReuseExisting = true;
        return clone;
      }

      return createFragmentFiber(returnFiber, childrenStorage, renderLanes);
    }

    if (isReactPortalObject(runtime, objectValue)) {
      bool reusedPortal = false;
      FiberNode* portalFiber = createFiberForPortalValue(
          runtime, returnFiber, existing, objectValue, renderLanes, reusedPortal);
      didReuseExisting = reusedPortal;
      return portalFiber;
    }
  }

  if (isThenable(runtime, childValue)) {
    Value resolved = unwrapThenable(runtime, childValue);
    bool innerReuse = false;
    FiberNode* resolvedFiber = createFiberForChildValue(
        runtime, returnFiber, existing, resolved, renderLanes, innerReuse);
    didReuseExisting = innerReuse;
    return resolvedFiber;
  }

  return nullptr;
}

FiberNode* reconcileChildrenArray(
    Runtime& runtime,
    FiberNode* currentFirstChild,
    FiberNode& workInProgress,
    const Array& nextChildren,
    Lanes renderLanes,
    bool shouldTrackSideEffects) {
  std::unordered_map<std::string, FiberNode*> existingChildren;
  for (FiberNode* child = currentFirstChild; child != nullptr; child = child->sibling) {
    existingChildren.emplace(fiberMapKey(*child), child);
  }

  FiberNode* firstNewChild = nullptr;
  FiberNode* previousNewChild = nullptr;
  int lastPlacedIndex = 0;

  const std::size_t length = nextChildren.size(runtime);
  for (std::size_t index = 0; index < length; ++index) {
    Value nextChild = nextChildren.getValueAtIndex(runtime, index);
    std::string lookupKey = childMapKey(runtime, nextChild, index);

    FiberNode* matchedExisting = nullptr;
    auto iter = existingChildren.find(lookupKey);
    if (iter != existingChildren.end()) {
      matchedExisting = iter->second;
    }

    bool didReuseExisting = false;
    FiberNode* newFiber = createFiberForChildValue(
        runtime, workInProgress, matchedExisting, nextChild, renderLanes, didReuseExisting);

    if (matchedExisting != nullptr) {
      existingChildren.erase(lookupKey);
      if (!didReuseExisting && shouldTrackSideEffects) {
        deleteChild(workInProgress, matchedExisting, shouldTrackSideEffects);
      }
    }

    if (newFiber == nullptr) {
      continue;
    }

    lastPlacedIndex = placeChildWithTracking(workInProgress, newFiber, lastPlacedIndex, index, shouldTrackSideEffects);

    if (firstNewChild == nullptr) {
      firstNewChild = newFiber;
    } else {
      previousNewChild->sibling = newFiber;
    }
    previousNewChild = newFiber;
  }

  if (previousNewChild != nullptr) {
    previousNewChild->sibling = nullptr;
  }

  if (shouldTrackSideEffects) {
    for (const auto& entry : existingChildren) {
      deleteChild(workInProgress, entry.second, shouldTrackSideEffects);
    }
  }

  recordChildForkIfHydrating(workInProgress, length);
  workInProgress.child = firstNewChild;
  return firstNewChild;
}

FiberNode* reconcileChildCollection(
    Runtime& runtime,
    FiberNode* currentFirstChild,
    FiberNode& workInProgress,
    const Value& nextChildren,
    Lanes renderLanes,
    bool shouldTrackSideEffects) {
  if (isNullLike(nextChildren)) {
    if (shouldTrackSideEffects) {
      deleteRemainingChildren(workInProgress, currentFirstChild, shouldTrackSideEffects);
    }
    workInProgress.child = nullptr;
    return nullptr;
  }

  if (isTextLike(nextChildren)) {
    FiberNode* child = reconcileSingleTextNode(
        runtime, currentFirstChild, workInProgress, nextChildren, renderLanes, shouldTrackSideEffects);
    workInProgress.child = child;
    return child;
  }

  if (jsx::isReactElementValue(runtime, nextChildren)) {
    auto element = jsx::getReactElementFromValue(runtime, nextChildren);
    FiberNode* child = reconcileSingleElement(
        runtime, currentFirstChild, workInProgress, *element, renderLanes, shouldTrackSideEffects);
    workInProgress.child = child;
    return child;
  }

  if (nextChildren.isObject()) {
    const Object objectValue = nextChildren.getObject(runtime);

    Value typeofValue = objectValue.getProperty(runtime, kTypeofProp);
    if (typeofValue.isSymbol()) {
      if (isSymbol(runtime, typeofValue, REACT_LAZY_TYPE)) {
        Value resolvedValue = resolveLazy(runtime, nextChildren);
        return reconcileChildCollection(
            runtime, currentFirstChild, workInProgress, resolvedValue, renderLanes, shouldTrackSideEffects);
      }

      if (isSymbol(runtime, typeofValue, REACT_CONTEXT_TYPE)) {
        Value resolvedValue = readContextDuringReconciliation(runtime, workInProgress, nextChildren, renderLanes);
        return reconcileChildCollection(
            runtime, currentFirstChild, workInProgress, resolvedValue, renderLanes, shouldTrackSideEffects);
      }
    }
    if (isReactPortalObject(runtime, objectValue)) {
      FiberNode* child = reconcileSinglePortal(
          runtime, currentFirstChild, workInProgress, objectValue, renderLanes, shouldTrackSideEffects);
      workInProgress.child = child;
      return child;
    }
    if (objectValue.isArray(runtime)) {
      Array arrayValue = objectValue.asArray(runtime);
      return reconcileChildrenArray(
          runtime, currentFirstChild, workInProgress, arrayValue, renderLanes, shouldTrackSideEffects);
    }

    Value iteratorFnValue = getIteratorFn(runtime, nextChildren);
    if (!iteratorFnValue.isNull() && !iteratorFnValue.isUndefined()) {
      Array collected = collectValuesFromIterator(runtime, nextChildren, iteratorFnValue);
      return reconcileChildrenArray(
          runtime, currentFirstChild, workInProgress, collected, renderLanes, shouldTrackSideEffects);
    }

    if (isThenable(runtime, nextChildren)) {
      Value resolved = unwrapThenable(runtime, nextChildren);
      return reconcileChildCollection(
          runtime, currentFirstChild, workInProgress, resolved, renderLanes, shouldTrackSideEffects);
    }
  }

  if (shouldTrackSideEffects) {
    deleteRemainingChildren(workInProgress, currentFirstChild, shouldTrackSideEffects);
  }
  workInProgress.child = nullptr;
  return nullptr;
}

} // namespace

FiberNode* cloneChildFibers(FiberNode* current, FiberNode& workInProgress) {
  if (current == nullptr) {
    workInProgress.child = nullptr;
    return nullptr;
  }

  FiberNode* currentChild = current->child;
  if (currentChild == nullptr) {
    workInProgress.child = nullptr;
    return nullptr;
  }

  if (workInProgress.child != nullptr && workInProgress.child != currentChild) {
    throw std::logic_error("Resuming work not yet implemented.");
  }

  FiberNode* firstNewChild = createWorkInProgress(currentChild, currentChild->pendingProps);
  if (firstNewChild == nullptr) {
    workInProgress.child = nullptr;
    return nullptr;
  }

  workInProgress.child = firstNewChild;
  firstNewChild->returnFiber = &workInProgress;

  FiberNode* previousNewChild = firstNewChild;
  FiberNode* previousCurrentChild = currentChild;

  while (previousCurrentChild->sibling != nullptr) {
    previousCurrentChild = previousCurrentChild->sibling;
    FiberNode* cloned = createWorkInProgress(previousCurrentChild, previousCurrentChild->pendingProps);
    if (cloned == nullptr) {
      previousNewChild->sibling = nullptr;
      return firstNewChild;
    }

    cloned->returnFiber = &workInProgress;
    previousNewChild->sibling = cloned;
    previousNewChild = cloned;
  }

  previousNewChild->sibling = nullptr;
  return firstNewChild;
}

void resetChildFibers(FiberNode& workInProgress, Lanes renderLanes) {
  for (FiberNode* child = workInProgress.child; child != nullptr; child = child->sibling) {
    resetWorkInProgress(child, renderLanes);
  }
}

FiberNode* mountChildFibers(
    ReactRuntime* reactRuntime,
    Runtime& runtime,
    FiberNode& workInProgress,
    const Value& nextChildren,
    Lanes renderLanes) {
  RuntimeScope runtimeScope(reactRuntime);
  ThenableScope thenableScope;
  return reconcileChildCollection(runtime, nullptr, workInProgress, nextChildren, renderLanes, false);
}

FiberNode* reconcileChildFibers(
    ReactRuntime* reactRuntime,
    Runtime& runtime,
    FiberNode* currentFirstChild,
    FiberNode& workInProgress,
    const Value& nextChildren,
    Lanes renderLanes) {
  RuntimeScope runtimeScope(reactRuntime);
  ThenableScope thenableScope;
  return reconcileChildCollection(runtime, currentFirstChild, workInProgress, nextChildren, renderLanes, true);
}

void* getPortalContainerInfo(FiberNode& fiber) {
  return getPortalContainerInfoImpl(fiber);
}

} // namespace react
