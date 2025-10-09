#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace react {

class FiberNode;
class ReactRuntime;

struct TreeContext {
	std::uint32_t id{1};
	std::string overflow;
};

void pushTreeFork(ReactRuntime& runtime, FiberNode& fiber, std::size_t totalChildren);
void popTreeContext(ReactRuntime& runtime, FiberNode& fiber);
std::size_t getForksAtLevel(ReactRuntime& runtime, const FiberNode& fiber);
bool isForkedChild(const FiberNode& fiber);
void handleForkedChildDuringHydration(ReactRuntime& runtime, FiberNode& fiber);
void pushTreeId(ReactRuntime& runtime, FiberNode& fiber, std::size_t totalChildren, std::size_t index);
void pushMaterializedTreeId(ReactRuntime& runtime, FiberNode& fiber);
std::optional<TreeContext> getSuspendedTreeContext(const ReactRuntime& runtime);
void restoreSuspendedTreeContext(ReactRuntime& runtime, FiberNode& fiber, const TreeContext& context);
std::string getTreeId(const ReactRuntime& runtime);

} // namespace react
