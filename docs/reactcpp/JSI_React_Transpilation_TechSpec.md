# ReactJS → JSI C++ 转译技术方案

> **版本**: 1.0.0  
> **日期**: 2025-12-27  
> **状态**: 设计中

---

## 1. 项目概述

### 1.1 目标

将 ReactJS 官方源码（`reactjs/packages/*`）完全 1:1 转译为基于 JSI (JavaScript Interface) 的 C++ 实现，实现：

- **文件级对应**：每个 JS 源文件对应一个同名 C++ 实现文件
- **模块级对应**：保持模块边界、导出接口完全一致
- **函数级对应**：每个导出/内部函数在 C++ 端有精确映射
- **逻辑级对应**：执行流程、算法逻辑逐语句对齐

### 1.2 适用范围

| 模块 | 优先级 | 说明 |
|------|--------|------|
| `shared` | P0 | 共享工具、常量、类型定义 |
| `react` | P0 | React 核心 API（createElement、Hooks 等） |
| `react-reconciler` | P0 | Fiber 协调器核心 |
| `scheduler` | P0 | 调度器实现 |
| `react-dom` | P1 | DOM 宿主配置（可选） |
| `react-dom-bindings` | P1 | DOM 事件/属性绑定 |

### 1.3 排除范围

- DevTools 相关包（`react-devtools-*`）
- 测试工具包（`jest-react`, `internal-test-utils`）
- ESLint 插件（`eslint-plugin-react-hooks`）
- Server Components（`react-server-*`）

---

## 2. 架构设计

### 2.1 整体架构

```
┌──────────────────────────────────────────────────────────────────┐
│                        Application Layer                          │
├──────────────────────────────────────────────────────────────────┤
│                         ReactCPP Core                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐               │
│  │    React    │  │  Reconciler │  │  Scheduler  │               │
│  │   (Hooks,   │  │   (Fiber,   │  │  (Priority, │               │
│  │  Elements)  │  │  WorkLoop)  │  │   TaskQueue)│               │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘               │
│         │                │                │                       │
│  ┌──────┴────────────────┴────────────────┴──────┐               │
│  │               Shared Utilities                 │               │
│  │  (ReactFeatureFlags, ReactWorkTags, etc.)      │               │
│  └────────────────────────┬──────────────────────┘               │
├───────────────────────────┼──────────────────────────────────────┤
│                           │                                       │
│  ┌────────────────────────┴──────────────────────┐               │
│  │           ReactHostRuntime Abstraction         │               │
│  │  ┌──────────────┐  ┌───────────────────┐      │               │
│  │  │  HostConfig  │  │   SchedulerHost   │      │               │
│  │  │  (Abstract)  │  │    (Abstract)     │      │               │
│  │  └──────────────┘  └───────────────────┘      │               │
│  └────────────────────────┬──────────────────────┘               │
├───────────────────────────┼──────────────────────────────────────┤
│                           │                                       │
│  ┌────────────────────────┴──────────────────────┐               │
│  │               JSI Runtime Layer                │               │
│  │  (facebook::jsi::Runtime, Value, Object, etc.) │               │
│  └────────────────────────────────────────────────┘               │
└──────────────────────────────────────────────────────────────────┘
```

### 2.2 核心组件

#### 2.2.1 ReactHostRuntime 抽象层

`ReactHostRuntime` 是一个抽象接口，为 React 核心提供宿主环境支持：

```cpp
// packages/React/src/runtime/ReactHostRuntime.h
#pragma once

#include <jsi/jsi.h>
#include <memory>
#include <functional>

namespace react {

/**
 * SchedulerHost - 调度器宿主抽象
 * 
 * 提供 Scheduler 所需的时间和调度原语，
 * 由具体宿主环境（浏览器、Native、Wasm）实现
 */
class SchedulerHost {
public:
    virtual ~SchedulerHost() = default;
    
    // 时间相关
    virtual double getCurrentTime() = 0;
    virtual double getTimeOrigin() = 0;
    
    // 调度原语
    virtual void requestHostCallback(std::function<bool(double)> callback) = 0;
    virtual void cancelHostCallback() = 0;
    virtual void requestHostTimeout(std::function<void()> callback, double ms) = 0;
    virtual void cancelHostTimeout() = 0;
    
    // MessageChannel 模拟
    virtual bool shouldYieldToHost() = 0;
    virtual void requestPaint() = 0;
    
    // 能力检测
    virtual bool supportsMessageChannel() = 0;
    virtual bool supportsIsInputPending() = 0;
};

/**
 * HostConfig - 宿主配置抽象
 * 
 * 定义 Reconciler 与具体渲染目标的交互接口
 */
class HostConfig {
public:
    virtual ~HostConfig() = default;
    
    // 实例操作
    using Instance = void*;
    using TextInstance = void*;
    using Container = void*;
    using ChildSet = void*;
    
    virtual Instance createInstance(
        const std::string& type,
        const facebook::jsi::Object& props,
        Container rootContainer,
        facebook::jsi::Runtime& runtime
    ) = 0;
    
    virtual TextInstance createTextInstance(
        const std::string& text,
        Container rootContainer,
        facebook::jsi::Runtime& runtime
    ) = 0;
    
    virtual void appendInitialChild(Instance parent, Instance child) = 0;
    virtual void appendChild(Instance parent, Instance child) = 0;
    virtual void removeChild(Instance parent, Instance child) = 0;
    virtual void insertBefore(Instance parent, Instance child, Instance beforeChild) = 0;
    
    // 属性更新
    virtual void commitUpdate(
        Instance instance,
        const facebook::jsi::Object& updatePayload,
        const std::string& type,
        const facebook::jsi::Object& oldProps,
        const facebook::jsi::Object& newProps,
        facebook::jsi::Runtime& runtime
    ) = 0;
    
    virtual void commitTextUpdate(
        TextInstance textInstance,
        const std::string& oldText,
        const std::string& newText
    ) = 0;
    
    // 容器操作
    virtual void appendChildToContainer(Container container, Instance child) = 0;
    virtual void removeChildFromContainer(Container container, Instance child) = 0;
    virtual void insertInContainerBefore(Container container, Instance child, Instance beforeChild) = 0;
    
    // 准备更新
    virtual facebook::jsi::Value prepareUpdate(
        Instance instance,
        const std::string& type,
        const facebook::jsi::Object& oldProps,
        const facebook::jsi::Object& newProps,
        facebook::jsi::Runtime& runtime
    ) = 0;
    
    // 能力检测
    virtual bool supportsMutation() { return true; }
    virtual bool supportsPersistence() { return false; }
    virtual bool supportsHydration() { return false; }
    virtual bool supportsMicrotasks() { return true; }
};

/**
 * ReactHostRuntime - 统一宿主运行时
 */
class ReactHostRuntime {
public:
    ReactHostRuntime(
        facebook::jsi::Runtime& jsiRuntime,
        std::unique_ptr<HostConfig> hostConfig,
        std::unique_ptr<SchedulerHost> schedulerHost
    );
    
    ~ReactHostRuntime() = default;
    
    facebook::jsi::Runtime& getJSIRuntime() { return jsiRuntime_; }
    HostConfig& getHostConfig() { return *hostConfig_; }
    SchedulerHost& getSchedulerHost() { return *schedulerHost_; }
    
    // 便捷方法
    double now() { return schedulerHost_->getCurrentTime(); }
    
private:
    facebook::jsi::Runtime& jsiRuntime_;
    std::unique_ptr<HostConfig> hostConfig_;
    std::unique_ptr<SchedulerHost> schedulerHost_;
};

} // namespace react
```

#### 2.2.2 模块映射策略

| ReactJS 模块路径 | C++ 目标路径 | 命名空间 |
|------------------|--------------|----------|
| `reactjs/packages/react/src/` | `packages/React/src/react/` | `react::core` |
| `reactjs/packages/react-reconciler/src/` | `packages/React/src/reconciler/` | `react::reconciler` |
| `reactjs/packages/scheduler/src/` | `packages/React/src/scheduler/` | `react::scheduler` |
| `reactjs/packages/shared/` | `packages/React/src/shared/` | `react::shared` |
| `reactjs/packages/react-dom/src/` | `packages/React/src/react-dom/` | `react::dom` |
| `reactjs/packages/react-dom-bindings/src/` | `packages/React/src/react-dom-bindings/` | `react::dom::bindings` |

---

## 3. 转译规则

### 3.1 文件对应规则

```
ReactJS 源文件                           C++ 目标文件
─────────────────────────────────────────────────────────────────
ReactFiberWorkLoop.js                 → ReactFiberWorkLoop.cpp/.h
ReactFiberWorkLoop.new.js             → ReactFiberWorkLoop.cpp/.h (合并)
ReactFiberWorkLoop.old.js             → 忽略（deprecated）
ReactXxx.js                           → ReactXxx.cpp/.h
__tests__/ReactXxx-test.js            → __tests__/ReactXxxTests.cpp
```

### 3.2 类型映射规则

| JavaScript 类型 | C++ 类型 | 说明 |
|----------------|----------|------|
| `number` | `double` / `int32_t` | 根据语义选择 |
| `string` | `std::string` | 或 `jsi::String` |
| `boolean` | `bool` | |
| `null` | `std::nullptr_t` / `std::nullopt` | |
| `undefined` | `std::nullopt` / 特殊标记 | |
| `object` | `jsi::Object` / `struct` | |
| `array` | `jsi::Array` / `std::vector<T>` | |
| `function` | `jsi::Function` / `std::function<>` | |
| `symbol` | `jsi::Symbol` | |
| `bigint` | `jsi::BigInt` / `int64_t` | |

### 3.3 Flow 类型映射

```javascript
// JavaScript (Flow)
type Fiber = {
  tag: WorkTag,
  key: null | string,
  elementType: any,
  type: any,
  stateNode: any,
  return: Fiber | null,
  child: Fiber | null,
  sibling: Fiber | null,
  index: number,
  // ...
};
```

```cpp
// C++ 等价结构
namespace react::reconciler {

struct Fiber {
    WorkTag tag;
    std::optional<std::string> key;
    facebook::jsi::Value elementType;
    facebook::jsi::Value type;
    facebook::jsi::Value stateNode;
    std::shared_ptr<Fiber> return_;     // 'return' 是 C++ 关键字
    std::shared_ptr<Fiber> child;
    std::shared_ptr<Fiber> sibling;
    int32_t index;
    // ...
};

} // namespace react::reconciler
```

### 3.4 导出/导入映射

```javascript
// JavaScript
export function performSyncWorkOnRoot(root) { ... }
export const SyncLane = 0b0001;
export default ReactFiberWorkLoop;
```

```cpp
// C++ Header (.h)
namespace react::reconciler {

void performSyncWorkOnRoot(FiberRoot* root);
constexpr Lane SyncLane = 0b0001;

} // namespace react::reconciler

// C++ Source (.cpp)
namespace react::reconciler {

void performSyncWorkOnRoot(FiberRoot* root) {
    // 实现
}

} // namespace react::reconciler
```

### 3.5 闭包与 Lambda 映射

```javascript
// JavaScript 闭包
function createWorkLoop() {
  let workInProgress = null;
  
  function performUnitOfWork(unitOfWork) {
    workInProgress = unitOfWork;
    // ...
  }
  
  return { performUnitOfWork };
}
```

```cpp
// C++ 等价实现
namespace react::reconciler {

class WorkLoop {
public:
    void performUnitOfWork(Fiber* unitOfWork) {
        workInProgress_ = unitOfWork;
        // ...
    }
    
private:
    Fiber* workInProgress_ = nullptr;
};

// 或使用 lambda 保持闭包语义
auto createWorkLoop() {
    auto workInProgress = std::make_shared<Fiber*>(nullptr);
    
    auto performUnitOfWork = [workInProgress](Fiber* unitOfWork) {
        *workInProgress = unitOfWork;
        // ...
    };
    
    return performUnitOfWork;
}

} // namespace react::reconciler
```

### 3.6 Feature Flag 处理

```javascript
// JavaScript
import { enableProfilerTimer } from 'shared/ReactFeatureFlags';

if (enableProfilerTimer) {
  recordCommitTime();
}
```

```cpp
// C++ - 编译时条件
#include "shared/ReactFeatureFlags.h"

#if REACT_ENABLE_PROFILER_TIMER
    recordCommitTime();
#endif

// 或运行时条件（当 flag 需要动态配置时）
if (ReactFeatureFlags::enableProfilerTimer()) {
    recordCommitTime();
}
```

---

## 4. JSI 集成规范

### 4.1 JSI Value 操作

```cpp
#include <jsi/jsi.h>

using namespace facebook::jsi;

// 创建值
Value createValue(Runtime& rt) {
    // 基本类型
    Value num = Value(42.0);
    Value str = String::createFromUtf8(rt, "hello");
    Value boolean = Value(true);
    Value null = Value::null();
    Value undefined = Value::undefined();
    
    // 对象
    Object obj(rt);
    obj.setProperty(rt, "key", "value");
    
    // 数组
    Array arr(rt, 3);
    arr.setValueAtIndex(rt, 0, 1);
    arr.setValueAtIndex(rt, 1, 2);
    arr.setValueAtIndex(rt, 2, 3);
    
    return obj;
}
```

### 4.2 HostFunction 注册

```cpp
// 注册 C++ 函数到 JS 运行时
void registerReactAPIs(Runtime& rt) {
    auto createElement = Function::createFromHostFunction(
        rt,
        PropNameID::forAscii(rt, "createElement"),
        3,  // 参数数量
        [](Runtime& rt, const Value& thisVal, const Value* args, size_t count) -> Value {
            // 实现 React.createElement
            return createReactElement(rt, args, count);
        }
    );
    
    rt.global().setProperty(rt, "React", ...);
}
```

### 4.3 HostObject 实现

```cpp
// Fiber 节点作为 HostObject
class FiberHostObject : public HostObject {
public:
    FiberHostObject(std::shared_ptr<Fiber> fiber) : fiber_(fiber) {}
    
    Value get(Runtime& rt, const PropNameID& name) override {
        auto propName = name.utf8(rt);
        if (propName == "tag") {
            return Value(static_cast<int>(fiber_->tag));
        }
        if (propName == "key") {
            return fiber_->key 
                ? String::createFromUtf8(rt, *fiber_->key)
                : Value::null();
        }
        // ...
        return Value::undefined();
    }
    
    void set(Runtime& rt, const PropNameID& name, const Value& value) override {
        // 实现属性设置
    }
    
    std::vector<PropNameID> getPropertyNames(Runtime& rt) override {
        return {
            PropNameID::forAscii(rt, "tag"),
            PropNameID::forAscii(rt, "key"),
            // ...
        };
    }
    
private:
    std::shared_ptr<Fiber> fiber_;
};
```

---

## 5. 模块对照表结构

### 5.1 对照表格式（JSONL）

每个源文件生成一行 JSON 记录：

```jsonl
{"sourceFile":"reactjs/packages/react/src/ReactHooks.js","sourceStartLine":45,"sourceEndLine":52,"symbolKind":"function","symbolName":"useState","isExported":true,"cppTargetFile":"packages/React/src/react/ReactHooks.cpp","cppSymbol":"react::core::useState","status":"not-started","testRequired":true}
{"sourceFile":"reactjs/packages/react/src/ReactHooks.js","sourceStartLine":54,"sourceEndLine":61,"symbolKind":"function","symbolName":"useEffect","isExported":true,"cppTargetFile":"packages/React/src/react/ReactHooks.cpp","cppSymbol":"react::core::useEffect","status":"not-started","testRequired":true}
```

### 5.2 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `sourceFile` | string | ReactJS 源文件相对路径 |
| `sourceStartLine` | number | 符号起始行号 |
| `sourceEndLine` | number | 符号结束行号 |
| `symbolKind` | enum | 符号类型：function, class, variable-export, arrow-fn, object-method |
| `symbolName` | string | 符号名称 |
| `isExported` | boolean | 是否为导出符号 |
| `cppTargetFile` | string | C++ 目标文件路径 |
| `cppSymbol` | string | C++ 完整符号名（含命名空间） |
| `status` | enum | 转译状态：not-started, in-progress, completed, blocked |
| `testRequired` | boolean | 是否需要单元测试 |

---

## 6. 测试策略

### 6.1 单元测试框架

使用 Google Test (gtest) 作为 C++ 单元测试框架：

```cpp
// packages/React/src/reconciler/__tests__/ReactFiberLaneTests.cpp

#include <gtest/gtest.h>
#include "reconciler/ReactFiberLane.h"

namespace react::reconciler::tests {

TEST(ReactFiberLaneTest, LaneConstants) {
    // 验证常量值与 JS 端一致
    EXPECT_EQ(NoLane, 0b0000000000000000000000000000000);
    EXPECT_EQ(SyncLane, 0b0000000000000000000000000000010);
    EXPECT_EQ(DefaultLane, 0b0000000000000000000000000100000);
}

TEST(ReactFiberLaneTest, MergeLanes) {
    Lane a = SyncLane;
    Lane b = DefaultLane;
    Lane merged = mergeLanes(a, b);
    
    EXPECT_TRUE(includesSomeLane(merged, SyncLane));
    EXPECT_TRUE(includesSomeLane(merged, DefaultLane));
}

TEST(ReactFiberLaneTest, GetHighestPriorityLane) {
    Lanes lanes = SyncLane | DefaultLane | IdleLane;
    Lane highest = getHighestPriorityLane(lanes);
    
    EXPECT_EQ(highest, SyncLane);
}

} // namespace react::reconciler::tests
```

### 6.2 集成测试

```cpp
// packages/React/src/__integration_tests__/ReactReconcilerIntegrationTests.cpp

#include <gtest/gtest.h>
#include "runtime/ReactHostRuntime.h"
#include "reconciler/ReactFiberReconciler.h"

class ReactReconcilerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建 mock JSI runtime
        runtime_ = createMockRuntime();
        hostConfig_ = std::make_unique<MockHostConfig>();
        schedulerHost_ = std::make_unique<MockSchedulerHost>();
        
        hostRuntime_ = std::make_unique<ReactHostRuntime>(
            *runtime_,
            std::move(hostConfig_),
            std::move(schedulerHost_)
        );
    }
    
    std::unique_ptr<facebook::jsi::Runtime> runtime_;
    std::unique_ptr<ReactHostRuntime> hostRuntime_;
};

TEST_F(ReactReconcilerTest, CreateRoot) {
    auto container = createContainer();
    auto root = createRoot(*hostRuntime_, container);
    
    EXPECT_NE(root, nullptr);
    EXPECT_EQ(root->containerInfo, container);
}
```

### 6.3 与 ReactJS 测试对照

每个 ReactJS 测试文件应有对应的 C++ 测试：

| ReactJS 测试 | C++ 测试 |
|--------------|----------|
| `react/src/__tests__/ReactElement-test.js` | `react/__tests__/ReactElementTests.cpp` |
| `react-reconciler/src/__tests__/ReactFiberLane-test.js` | `reconciler/__tests__/ReactFiberLaneTests.cpp` |
| `scheduler/src/__tests__/Scheduler-test.js` | `scheduler/__tests__/SchedulerTests.cpp` |

---

## 7. 构建系统

### 7.1 CMake 配置

```cmake
# packages/React/CMakeLists.txt

cmake_minimum_required(VERSION 3.16)
project(ReactCPP VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 依赖
find_package(GTest REQUIRED)

# 编译选项
option(REACT_ENABLE_PROFILER_TIMER "Enable profiler timing" ON)
option(REACT_ENABLE_DEBUG_TRACING "Enable debug tracing" OFF)
option(REACT_ENABLE_EXPERIMENTAL "Enable experimental features" OFF)

# 配置头文件
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/ReactFeatureFlagsConfig.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/shared/ReactFeatureFlagsConfig.h
)

# 源文件
set(REACT_SHARED_SOURCES
    src/shared/ReactFeatureFlags.cpp
    src/shared/ReactWorkTags.cpp
    src/shared/ReactSymbols.cpp
    src/shared/ReactSharedInternals.cpp
)

set(REACT_CORE_SOURCES
    src/react/ReactElement.cpp
    src/react/ReactHooks.cpp
    src/react/ReactContext.cpp
)

set(REACT_RECONCILER_SOURCES
    src/reconciler/ReactFiber.cpp
    src/reconciler/ReactFiberLane.cpp
    src/reconciler/ReactFiberWorkLoop.cpp
    src/reconciler/ReactFiberBeginWork.cpp
    src/reconciler/ReactFiberCompleteWork.cpp
    src/reconciler/ReactFiberCommitWork.cpp
)

set(REACT_SCHEDULER_SOURCES
    src/scheduler/Scheduler.cpp
    src/scheduler/SchedulerMinHeap.cpp
    src/scheduler/SchedulerPriorities.cpp
)

# 库
add_library(ReactCPP
    ${REACT_SHARED_SOURCES}
    ${REACT_CORE_SOURCES}
    ${REACT_RECONCILER_SOURCES}
    ${REACT_SCHEDULER_SOURCES}
)

target_include_directories(ReactCPP PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../jsi
)

target_link_libraries(ReactCPP PUBLIC jsi)

# 测试
enable_testing()

add_executable(ReactCPPTests
    src/shared/__tests__/ReactFeatureFlagsTests.cpp
    src/reconciler/__tests__/ReactFiberLaneTests.cpp
    src/scheduler/__tests__/SchedulerTests.cpp
)

target_link_libraries(ReactCPPTests ReactCPP GTest::gtest_main)

gtest_discover_tests(ReactCPPTests)
```

---

## 8. 质量保证

### 8.1 代码覆盖率目标

| 模块 | 最低覆盖率 |
|------|-----------|
| shared | 95% |
| react | 90% |
| reconciler | 85% |
| scheduler | 90% |

### 8.2 CI/CD 检查项

1. **编译检查**：所有平台（Linux、macOS、Windows）编译通过
2. **单元测试**：所有测试通过
3. **代码覆盖率**：达到目标覆盖率
4. **Parity 检查**：与 ReactJS 函数映射表一致性校验
5. **静态分析**：clang-tidy 无警告

### 8.3 文档要求

每个翻译的函数必须包含：

```cpp
/**
 * @brief 执行同步渲染工作
 * 
 * @source reactjs/packages/react-reconciler/src/ReactFiberWorkLoop.js:123-156
 * @param root 要渲染的 FiberRoot
 * @return 渲染结果
 * 
 * @note 与 JS 端 performSyncWorkOnRoot 完全对齐
 * @see https://github.com/facebook/react/blob/main/packages/react-reconciler/src/ReactFiberWorkLoop.js#L123
 */
void performSyncWorkOnRoot(FiberRoot* root) {
    // ...
}
```

---

## 9. 附录

### 9.1 参考资料

- [React GitHub Repository](https://github.com/facebook/react)
- [JSI (JavaScript Interface) 文档](https://github.com/nicklockwood/jsi)
- [React 源码解析](https://react.iamkasong.com/)

### 9.2 术语表

| 术语 | 英文 | 说明 |
|------|------|------|
| 协调器 | Reconciler | React 的核心调度算法 |
| Fiber | Fiber | React 的工作单元数据结构 |
| Lane | Lane | React 的优先级模型 |
| 调度器 | Scheduler | 任务调度系统 |
| 宿主配置 | HostConfig | 渲染目标的接口定义 |
| JSI | JavaScript Interface | Meta 的 C++ ↔ JS 桥接库 |

### 9.3 版本历史

| 版本 | 日期 | 作者 | 变更说明 |
|------|------|------|----------|
| 1.0.0 | 2025-12-27 | - | 初始版本 |
