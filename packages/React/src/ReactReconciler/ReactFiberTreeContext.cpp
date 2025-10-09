#include "ReactReconciler/ReactFiberTreeContext.h"

#include "ReactReconciler/ReactFiber.h"
#include "ReactReconciler/ReactFiberFlags.h"
#include "ReactRuntime/ReactRuntime.h"

#include <algorithm>
#include <utility>

namespace react {
namespace {

constexpr char kBase32Alphabet[] = "0123456789abcdefghijklmnopqrstuv";

WorkLoopState& getTreeState(ReactRuntime& runtime) {
  return runtime.workLoopState();
}

const WorkLoopState& getTreeState(const ReactRuntime& runtime) {
  return runtime.workLoopState();
}

int getBitLength(std::uint32_t value) {
  int length = 0;
  while (value != 0) {
    value >>= 1;
    ++length;
  }
  return length == 0 ? 1 : length;
}

std::uint32_t getLeadingBit(std::uint32_t idWithLeadingBit) {
  return 1u << (getBitLength(idWithLeadingBit) - 1);
}

std::string toBase32(std::uint32_t value) {
  if (value == 0) {
    return std::string("0");
  }

  std::string result;
  while (value > 0) {
    const int digit = static_cast<int>(value % 32u);
    result.push_back(kBase32Alphabet[digit]);
    value /= 32u;
  }
  std::reverse(result.begin(), result.end());
  return result;
}

} // namespace

void pushTreeFork(ReactRuntime& runtime, FiberNode& fiber, std::size_t totalChildren) {
  auto& state = getTreeState(runtime);
  state.treeForkStack.push_back({state.treeForkProvider, state.treeForkCount});
  state.treeForkProvider = &fiber;
  state.treeForkCount = totalChildren;
}

void popTreeContext(ReactRuntime& runtime, FiberNode& fiber) {
  auto& state = getTreeState(runtime);

  while (state.treeForkProvider == &fiber && !state.treeForkStack.empty()) {
    const TreeForkEntry entry = state.treeForkStack.back();
    state.treeForkStack.pop_back();
    state.treeForkProvider = entry.provider;
    state.treeForkCount = entry.forkCount;
  }

  while (state.treeContextProvider == &fiber && !state.treeIdStack.empty()) {
    const TreeIdEntry entry = state.treeIdStack.back();
    state.treeIdStack.pop_back();
    state.treeContextProvider = entry.provider;
    state.treeContextId = entry.id;
    state.treeContextOverflow = entry.overflow;
  }
}

std::size_t getForksAtLevel(ReactRuntime& runtime, const FiberNode& /*fiber*/) {
  return getTreeState(runtime).treeForkCount;
}

bool isForkedChild(const FiberNode& fiber) {
  return (fiber.flags & Forked) != NoFlags;
}

void pushTreeId(ReactRuntime& runtime, FiberNode& fiber, std::size_t totalChildren, std::size_t index) {
  auto& state = getTreeState(runtime);
  if (totalChildren == 0) {
    totalChildren = 1;
  }

  state.treeIdStack.push_back({state.treeContextProvider, state.treeContextId, state.treeContextOverflow});
  state.treeContextProvider = &fiber;

  const std::uint32_t baseIdWithLeadingBit = state.treeContextId;
  const std::string baseOverflow = state.treeContextOverflow;

  const int baseLength = getBitLength(baseIdWithLeadingBit) - 1;
  const std::uint32_t baseId = baseIdWithLeadingBit & ~getLeadingBit(baseIdWithLeadingBit);

  const std::uint32_t slot = static_cast<std::uint32_t>(index + 1);
  const int forkLength = getBitLength(static_cast<std::uint32_t>(totalChildren));
  const int totalLength = baseLength + forkLength;

  if (totalLength > 30) {
    const int overflowBitCount = baseLength - (baseLength % 5);
    std::uint32_t overflowMask = 0;
    if (overflowBitCount > 0) {
      overflowMask = (1u << overflowBitCount) - 1u;
    }

    std::string newOverflowSegment;
    if (overflowMask != 0) {
      const std::uint32_t newOverflowBits = baseId & overflowMask;
      newOverflowSegment = toBase32(newOverflowBits);
    }

    const std::uint32_t restOfBaseId = overflowMask == 0 ? baseId : (baseId >> overflowBitCount);
    const int restOfBaseLength = baseLength - overflowBitCount;
    const int restLength = forkLength + restOfBaseLength;

    const std::uint32_t restNewBits = slot << restOfBaseLength;
    const std::uint32_t id = restNewBits | restOfBaseId;

    state.treeContextId = (1u << restLength) | id;
    state.treeContextOverflow = newOverflowSegment + baseOverflow;
  } else {
    const std::uint32_t newBits = slot << baseLength;
    const std::uint32_t id = newBits | baseId;
    state.treeContextId = (1u << totalLength) | id;
    state.treeContextOverflow = baseOverflow;
  }
}

void pushMaterializedTreeId(ReactRuntime& runtime, FiberNode& fiber) {
  if (fiber.returnFiber == nullptr) {
    return;
  }
  pushTreeFork(runtime, fiber, 1);
  pushTreeId(runtime, fiber, 1, 0);
}

std::optional<TreeContext> getSuspendedTreeContext(const ReactRuntime& runtime) {
  const auto& state = getTreeState(runtime);
  if (state.treeContextProvider != nullptr) {
    return TreeContext{state.treeContextId, state.treeContextOverflow};
  }
  return std::nullopt;
}

void restoreSuspendedTreeContext(ReactRuntime& runtime, FiberNode& fiber, const TreeContext& context) {
  auto& state = getTreeState(runtime);
  state.treeIdStack.push_back({state.treeContextProvider, state.treeContextId, state.treeContextOverflow});
  state.treeContextProvider = &fiber;
  state.treeContextId = context.id;
  state.treeContextOverflow = context.overflow;
}

std::string getTreeId(const ReactRuntime& runtime) {
  const auto& state = getTreeState(runtime);
  const std::uint32_t idWithLeadingBit = state.treeContextId;
  const std::uint32_t id = idWithLeadingBit & ~getLeadingBit(idWithLeadingBit);
  return toBase32(id) + state.treeContextOverflow;
}

void handleForkedChildDuringHydration(ReactRuntime& runtime, FiberNode& fiber) {
  auto& state = getTreeState(runtime);
  FiberNode* const parent = fiber.returnFiber;
  if (parent == nullptr || state.treeForkProvider != parent) {
    return;
  }

  const std::size_t totalChildren = state.treeForkCount;
  pushTreeId(runtime, fiber, totalChildren == 0 ? 1 : totalChildren, fiber.index);
}

} // namespace react
