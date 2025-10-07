#pragma once

#include "ReactReconciler/ReactWakeable.h"

#include "jsi/jsi.h"

#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <vector>

namespace react {

namespace jsi = facebook::jsi;

Wakeable& noopSuspenseyCommitThenable();
bool isNoopSuspenseyCommitThenable(const Wakeable* wakeable);
bool isNoopSuspenseyCommitThenable(const void* value);

class SuspenseException final : public std::exception {
 public:
	const char* what() const noexcept override;
};

class SuspenseyCommitException final : public std::exception {
 public:
	const char* what() const noexcept override;
};

class SuspenseActionException final : public std::exception {
 public:
	const char* what() const noexcept override;
};

const SuspenseException& suspenseException();
const SuspenseyCommitException& suspenseyCommitException();
const SuspenseActionException& suspenseActionException();

[[noreturn]] void throwSuspenseException();
[[noreturn]] void throwSuspenseyCommitException();
[[noreturn]] void throwSuspenseActionException();
[[noreturn]] void suspendCommit();

struct ThenableState {
#if !defined(NDEBUG)
	bool didWarnAboutUncachedPromise{false};
#endif
	std::vector<std::unique_ptr<jsi::Value>> thenables;
};

ThenableState createThenableState(jsi::Runtime& runtime);
bool isThenableResolved(jsi::Runtime& runtime, const jsi::Value& thenableValue);

jsi::Value trackUsedThenable(
		jsi::Runtime& runtime,
		ThenableState& state,
		const jsi::Value& thenableValue,
		std::size_t index);

void setSuspendedThenable(const jsi::Value& thenable, jsi::Runtime& runtime);
jsi::Value getSuspendedThenable(jsi::Runtime& runtime);
bool hasSuspendedThenable();

bool checkIfUseWrappedInTryCatch();
void checkIfUseWrappedInAsyncCatch(jsi::Runtime& runtime, const jsi::Value& rejectedReason);

jsi::Value resolveLazy(jsi::Runtime& runtime, const jsi::Value& lazyValue);

} // namespace react
