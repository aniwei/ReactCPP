/**
 * React Error Handling 测试
 * 
 * 测试错误边界、Throw、Unwind 和 CapturedValue
 */

#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>

#include "../ReactCapturedValue.h"
#include "../ReactFiberThrow.h"
#include "../ReactFiberUnwindWork.h"
#include "../ReactFiber.h"
#include "../ReactFiberRoot.h"
#include "../ReactFiberLane.h"
#include "../ReactWorkTags.h"
#include "../ReactFiberFlags.h"
#include "../ReactTypeOfMode.h"

using namespace react::reconciler;

// 测试辅助: 创建测试用 Fiber
inline FiberRef makeTestFiber(WorkTag tag, TypeOfMode mode = NoMode) {
  return std::make_shared<Fiber>(tag, mode);
}

// =============================================================================
// CapturedValue 测试
// =============================================================================

class CapturedValueTest : public ::testing::Test {};

TEST_F(CapturedValueTest, CreateCapturedValueAtFiber) {
  auto fiber = makeTestFiber(FunctionComponent);
  
  auto captured = createCapturedValueAtFiber<std::string>("Test error", fiber);
  
  EXPECT_NE(captured, nullptr);
  EXPECT_EQ(captured->value, "Test error");
  EXPECT_TRUE(captured->stack.has_value());
  EXPECT_TRUE(captured->stack->find("FunctionComponent") != std::string::npos);
}

TEST_F(CapturedValueTest, CreateCapturedValueWithStack) {
  auto root = makeTestFiber(HostRoot);
  auto parent = makeTestFiber(ClassComponent);
  auto child = makeTestFiber(FunctionComponent);
  
  parent->return_ = root;
  child->return_ = parent;
  
  auto captured = createCapturedValueAtFiber<int>(42, child);
  
  EXPECT_EQ(captured->value, 42);
  EXPECT_TRUE(captured->stack.has_value());
  // 栈应该包含所有组件
  EXPECT_TRUE(captured->stack->find("FunctionComponent") != std::string::npos);
  EXPECT_TRUE(captured->stack->find("ClassComponent") != std::string::npos);
  EXPECT_TRUE(captured->stack->find("HostRoot") != std::string::npos);
}

TEST_F(CapturedValueTest, CreateCapturedValueFromError) {
  try {
    throw std::runtime_error("Test exception");
  } catch (...) {
    auto error = std::current_exception();
    auto captured = createCapturedValueFromError(error, "custom stack");
    
    EXPECT_NE(captured, nullptr);
    EXPECT_TRUE(captured->message.has_value());
    EXPECT_EQ(captured->message.value(), "Test exception");
    EXPECT_EQ(captured->stack.value(), "custom stack");
  }
}

TEST_F(CapturedValueTest, CreateCapturedValueWithDigest) {
  try {
    throw std::runtime_error("Digest error");
  } catch (...) {
    auto error = std::current_exception();
    auto captured = createCapturedValueWithDigest(error, "error-digest-123");
    
    EXPECT_TRUE(captured->digest.has_value());
    EXPECT_EQ(captured->digest.value(), "error-digest-123");
  }
}

// =============================================================================
// FiberUpdate 测试
// =============================================================================

class FiberUpdateTest : public ::testing::Test {};

TEST_F(FiberUpdateTest, CreateFiberUpdate) {
  auto update = createFiberUpdate<std::any>(DefaultLane);
  
  EXPECT_NE(update, nullptr);
  EXPECT_EQ(update->lane, DefaultLane);
  EXPECT_EQ(update->tag, UpdateTag::UpdateState);
  EXPECT_EQ(update->callback, nullptr);
  EXPECT_EQ(update->next, nullptr);
}

TEST_F(FiberUpdateTest, CreateRootErrorUpdate) {
  auto root = std::make_shared<FiberRoot>();
  auto fiber = makeTestFiber(HostRoot);
  root->current = fiber;
  
  try {
    throw std::runtime_error("Root error");
  } catch (...) {
    auto errorInfo = createCapturedValueFromError(std::current_exception());
    auto update = createRootErrorUpdate(root, errorInfo, SyncLane);
    
    EXPECT_NE(update, nullptr);
    EXPECT_EQ(update->tag, UpdateTag::CaptureUpdate);
    EXPECT_EQ(update->lane, SyncLane);
    EXPECT_NE(update->callback, nullptr);
  }
}

TEST_F(FiberUpdateTest, CreateClassErrorUpdate) {
  auto update = createClassErrorUpdate(DefaultLane);
  
  EXPECT_NE(update, nullptr);
  EXPECT_EQ(update->tag, UpdateTag::CaptureUpdate);
  EXPECT_EQ(update->lane, DefaultLane);
}

// =============================================================================
// Suspense 边界标记测试
// =============================================================================

class SuspenseBoundaryTest : public ::testing::Test {};

TEST_F(SuspenseBoundaryTest, MarkShouldCaptureInConcurrentMode) {
  auto root = std::make_shared<FiberRoot>();
  auto rootFiber = makeTestFiber(HostRoot, ConcurrentMode);
  root->current = rootFiber;
  
  auto suspenseBoundary = makeTestFiber(SuspenseComponent, ConcurrentMode);
  auto sourceFiber = makeTestFiber(FunctionComponent, ConcurrentMode);
  
  suspenseBoundary->return_ = rootFiber;
  sourceFiber->return_ = suspenseBoundary;
  
  auto result = markSuspenseBoundaryShouldCapture(
    suspenseBoundary,
    suspenseBoundary,
    sourceFiber,
    root,
    DefaultLane
  );
  
  EXPECT_EQ(result, suspenseBoundary);
  EXPECT_TRUE((suspenseBoundary->flags & ShouldCapture) != NoFlags);
}

TEST_F(SuspenseBoundaryTest, MarkDidCaptureInLegacyMode) {
  auto root = std::make_shared<FiberRoot>();
  auto rootFiber = makeTestFiber(HostRoot, NoMode);
  root->current = rootFiber;
  
  auto suspenseBoundary = makeTestFiber(SuspenseComponent, NoMode);
  auto returnFiber = makeTestFiber(HostComponent, NoMode);
  auto sourceFiber = makeTestFiber(FunctionComponent, NoMode);
  
  sourceFiber->return_ = returnFiber;
  returnFiber->return_ = suspenseBoundary;
  
  auto result = markSuspenseBoundaryShouldCapture(
    suspenseBoundary,
    returnFiber,  // 不同于 suspenseBoundary
    sourceFiber,
    root,
    DefaultLane
  );
  
  EXPECT_EQ(result, suspenseBoundary);
  EXPECT_TRUE((suspenseBoundary->flags & DidCapture) != NoFlags);
  EXPECT_TRUE((sourceFiber->flags & ForceUpdateForLegacySuspense) != NoFlags);
}

// =============================================================================
// ResetSuspendedComponent 测试
// =============================================================================

class ResetSuspendedComponentTest : public ::testing::Test {};

TEST_F(ResetSuspendedComponentTest, ResetFunctionComponentInLegacyMode) {
  auto sourceFiber = makeTestFiber(FunctionComponent, NoMode);
  auto alternate = makeTestFiber(FunctionComponent, NoMode);
  
  alternate->memoizedState = std::string("original state");
  alternate->lanes = InputContinuousLane;
  sourceFiber->alternate = alternate;
  
  resetSuspendedComponent(sourceFiber, DefaultLane);
  
  // 在遗留模式下，状态应该从 alternate 恢复
  EXPECT_EQ(sourceFiber->lanes, InputContinuousLane);
}

TEST_F(ResetSuspendedComponentTest, NoResetInConcurrentMode) {
  auto sourceFiber = makeTestFiber(FunctionComponent, ConcurrentMode);
  sourceFiber->lanes = SyncLane;
  
  resetSuspendedComponent(sourceFiber, DefaultLane);
  
  // 在并发模式下，不应该重置
  EXPECT_EQ(sourceFiber->lanes, SyncLane);
}

// =============================================================================
// Legacy Error Boundary 测试
// =============================================================================

class LegacyErrorBoundaryTest : public ::testing::Test {
protected:
  void SetUp() override {
    clearLegacyErrorBoundaries();
  }
  void TearDown() override {
    clearLegacyErrorBoundaries();
  }
};

TEST_F(LegacyErrorBoundaryTest, MarkAndCheck) {
  auto fiber = makeTestFiber(ClassComponent);
  
  EXPECT_FALSE(isAlreadyFailedLegacyErrorBoundary(fiber));
  
  markLegacyErrorBoundaryAsFailed(fiber);
  
  EXPECT_TRUE(isAlreadyFailedLegacyErrorBoundary(fiber));
}

TEST_F(LegacyErrorBoundaryTest, ClearBoundaries) {
  auto fiber1 = makeTestFiber(ClassComponent);
  auto fiber2 = makeTestFiber(ClassComponent);
  
  markLegacyErrorBoundaryAsFailed(fiber1);
  markLegacyErrorBoundaryAsFailed(fiber2);
  
  EXPECT_TRUE(isAlreadyFailedLegacyErrorBoundary(fiber1));
  EXPECT_TRUE(isAlreadyFailedLegacyErrorBoundary(fiber2));
  
  clearLegacyErrorBoundaries();
  
  EXPECT_FALSE(isAlreadyFailedLegacyErrorBoundary(fiber1));
  EXPECT_FALSE(isAlreadyFailedLegacyErrorBoundary(fiber2));
}

// =============================================================================
// UnwindWork 测试
// =============================================================================

class UnwindWorkTest : public ::testing::Test {};

TEST_F(UnwindWorkTest, UnwindClassComponentWithShouldCapture) {
  auto workInProgress = makeTestFiber(ClassComponent, ConcurrentMode);
  workInProgress->flags = ShouldCapture;
  
  auto result = unwindWork(nullptr, workInProgress, DefaultLane);
  
  EXPECT_EQ(result, workInProgress);
  EXPECT_TRUE((workInProgress->flags & DidCapture) != NoFlags);
  EXPECT_FALSE((workInProgress->flags & ShouldCapture) != NoFlags);
}

TEST_F(UnwindWorkTest, UnwindClassComponentWithoutCapture) {
  auto workInProgress = makeTestFiber(ClassComponent, ConcurrentMode);
  workInProgress->flags = NoFlags;
  
  auto result = unwindWork(nullptr, workInProgress, DefaultLane);
  
  EXPECT_EQ(result, nullptr);
}

TEST_F(UnwindWorkTest, UnwindSuspenseComponentWithShouldCapture) {
  auto workInProgress = makeTestFiber(SuspenseComponent, ConcurrentMode);
  workInProgress->flags = ShouldCapture;
  
  auto result = unwindWork(nullptr, workInProgress, DefaultLane);
  
  EXPECT_EQ(result, workInProgress);
  EXPECT_TRUE((workInProgress->flags & DidCapture) != NoFlags);
}

TEST_F(UnwindWorkTest, UnwindHostRoot) {
  auto workInProgress = makeTestFiber(HostRoot, ConcurrentMode);
  auto root = std::make_shared<FiberRoot>();
  root->current = workInProgress;
  workInProgress->stateNode = root;
  workInProgress->flags = ShouldCapture;
  
  auto result = unwindWork(nullptr, workInProgress, DefaultLane);
  
  EXPECT_EQ(result, workInProgress);
  EXPECT_TRUE((workInProgress->flags & DidCapture) != NoFlags);
}

TEST_F(UnwindWorkTest, UnwindHostComponent) {
  auto workInProgress = makeTestFiber(HostComponent, ConcurrentMode);
  
  auto result = unwindWork(nullptr, workInProgress, DefaultLane);
  
  EXPECT_EQ(result, nullptr);
}

TEST_F(UnwindWorkTest, UnwindSuspenseList) {
  auto workInProgress = makeTestFiber(SuspenseListComponent, ConcurrentMode);
  
  auto result = unwindWork(nullptr, workInProgress, DefaultLane);
  
  // SuspenseList 不捕获
  EXPECT_EQ(result, nullptr);
}

// =============================================================================
// UnwindInterruptedWork 测试
// =============================================================================

class UnwindInterruptedWorkTest : public ::testing::Test {};

TEST_F(UnwindInterruptedWorkTest, UnwindHostRoot) {
  auto interruptedWork = makeTestFiber(HostRoot, ConcurrentMode);
  auto root = std::make_shared<FiberRoot>();
  root->current = interruptedWork;
  interruptedWork->stateNode = root;
  
  // 不应该抛出异常
  EXPECT_NO_THROW(unwindInterruptedWork(nullptr, interruptedWork, DefaultLane));
}

TEST_F(UnwindInterruptedWorkTest, UnwindSuspenseComponent) {
  auto interruptedWork = makeTestFiber(SuspenseComponent, ConcurrentMode);
  
  EXPECT_NO_THROW(unwindInterruptedWork(nullptr, interruptedWork, DefaultLane));
}

// =============================================================================
// CompleteUnitOfUnwind 测试
// =============================================================================

class CompleteUnitOfUnwindTest : public ::testing::Test {};

TEST_F(CompleteUnitOfUnwindTest, FindsCapturingBoundary) {
  auto unitOfWork = makeTestFiber(SuspenseComponent, ConcurrentMode);
  unitOfWork->flags = ShouldCapture | Incomplete;
  
  auto result = completeUnitOfUnwind(unitOfWork, nullptr, DefaultLane);
  
  EXPECT_EQ(result, unitOfWork);
  EXPECT_FALSE((result->flags & Incomplete) != NoFlags);
}

TEST_F(CompleteUnitOfUnwindTest, MarksParentIncomplete) {
  auto parent = makeTestFiber(HostComponent, ConcurrentMode);
  auto child = makeTestFiber(FunctionComponent, ConcurrentMode);
  child->return_ = parent;
  child->flags = NoFlags;
  
  auto result = completeUnitOfUnwind(child, nullptr, DefaultLane);
  
  EXPECT_EQ(result, nullptr);
  EXPECT_TRUE((parent->flags & Incomplete) != NoFlags);
}

// =============================================================================
// ThrownException 测试
// =============================================================================

class ThrownExceptionTest : public ::testing::Test {};

TEST_F(ThrownExceptionTest, DefaultValues) {
  ThrownException exception;
  
  EXPECT_EQ(exception.type, ThrownExceptionType::Error);
  EXPECT_FALSE(exception.value.has_value());
  EXPECT_EQ(exception.capturedValue, nullptr);
}

TEST_F(ThrownExceptionTest, ErrorType) {
  ThrownException exception;
  exception.type = ThrownExceptionType::Error;
  exception.value = std::string("Error message");
  
  EXPECT_EQ(exception.type, ThrownExceptionType::Error);
  EXPECT_EQ(std::any_cast<std::string>(exception.value), "Error message");
}

TEST_F(ThrownExceptionTest, SuspenseType) {
  ThrownException exception;
  exception.type = ThrownExceptionType::Suspense;
  
  EXPECT_EQ(exception.type, ThrownExceptionType::Suspense);
}

// =============================================================================
// isThenable 测试
// =============================================================================

class IsThenableTest : public ::testing::Test {};

TEST_F(IsThenableTest, EmptyValueNotThenable) {
  std::any value;
  EXPECT_FALSE(isThenable(value));
}

TEST_F(IsThenableTest, StringNotThenable) {
  std::any value = std::string("test");
  EXPECT_FALSE(isThenable(value));
}

TEST_F(IsThenableTest, ThenableIsThenable) {
  auto thenable = std::make_shared<Thenable<std::any>>();
  std::any value = thenable;
  
  EXPECT_TRUE(isThenable(value));
}

// =============================================================================
// 综合场景测试
// =============================================================================

class ErrorHandlingIntegrationTest : public ::testing::Test {
protected:
  void SetUp() override {
    clearLegacyErrorBoundaries();
  }
  void TearDown() override {
    clearLegacyErrorBoundaries();
  }
};

TEST_F(ErrorHandlingIntegrationTest, ErrorBoundaryCapture) {
  // 创建 Fiber 树: HostRoot -> ErrorBoundary (ClassComponent) -> Child
  auto root = makeTestFiber(HostRoot, ConcurrentMode);
  auto errorBoundary = makeTestFiber(ClassComponent, ConcurrentMode);
  auto child = makeTestFiber(FunctionComponent, ConcurrentMode);
  
  auto fiberRoot = std::make_shared<FiberRoot>();
  fiberRoot->current = root;
  root->stateNode = fiberRoot;
  
  errorBoundary->return_ = root;
  child->return_ = errorBoundary;
  
  // 模拟 child 抛出错误
  child->flags = Incomplete;
  errorBoundary->flags = ShouldCapture;
  
  // Unwind 应该在 errorBoundary 停止
  auto result = unwindWork(nullptr, errorBoundary, DefaultLane);
  
  EXPECT_EQ(result, errorBoundary);
  EXPECT_TRUE((errorBoundary->flags & DidCapture) != NoFlags);
}

TEST_F(ErrorHandlingIntegrationTest, SuspenseCapture) {
  // 创建 Fiber 树: HostRoot -> Suspense -> Child
  auto root = makeTestFiber(HostRoot, ConcurrentMode);
  auto suspense = makeTestFiber(SuspenseComponent, ConcurrentMode);
  auto child = makeTestFiber(FunctionComponent, ConcurrentMode);
  
  auto fiberRoot = std::make_shared<FiberRoot>();
  fiberRoot->current = root;
  root->stateNode = fiberRoot;
  
  suspense->return_ = root;
  child->return_ = suspense;
  
  // 模拟 child suspend
  child->flags = Incomplete;
  suspense->flags = ShouldCapture;
  
  auto result = unwindWork(nullptr, suspense, DefaultLane);
  
  EXPECT_EQ(result, suspense);
  EXPECT_TRUE((suspense->flags & DidCapture) != NoFlags);
}

TEST_F(ErrorHandlingIntegrationTest, FullStackUnwind) {
  // 创建更深的 Fiber 树
  auto root = makeTestFiber(HostRoot, ConcurrentMode);
  auto app = makeTestFiber(FunctionComponent, ConcurrentMode);
  auto errorBoundary = makeTestFiber(ClassComponent, ConcurrentMode);
  auto container = makeTestFiber(HostComponent, ConcurrentMode);
  auto child = makeTestFiber(FunctionComponent, ConcurrentMode);
  
  auto fiberRoot = std::make_shared<FiberRoot>();
  fiberRoot->current = root;
  root->stateNode = fiberRoot;
  
  app->return_ = root;
  errorBoundary->return_ = app;
  container->return_ = errorBoundary;
  child->return_ = container;
  
  // Child 抛出错误，向上遍历直到 errorBoundary
  // 首先展开 container（不捕获）
  auto result1 = unwindWork(nullptr, container, DefaultLane);
  EXPECT_EQ(result1, nullptr);
  
  // 然后展开 errorBoundary（捕获）
  errorBoundary->flags = ShouldCapture;
  auto result2 = unwindWork(nullptr, errorBoundary, DefaultLane);
  EXPECT_EQ(result2, errorBoundary);
}
