# React-CPP 1:1 Translation Roadmap（react-main ➜ C++/JSI/Wasm）

> 目标：以 React 官方仓库 `react-main` 为单一事实来源（SSOT），在 C++/JSI/Wasm 侧逐文件、逐函数复刻，实现逻辑、命名、结构 100% 对齐，确保任一差异都可追溯。

## 0. 愿景与原则

### 绝对对齐原则
- **文件命名一一映射**：保持 `ReactFiberWorkLoop.new.js` ➜ `ReactFiberWorkLoop.cpp` 等 1:1 对应，目录结构采用 `packages/react-main` 的相对路径。
- **函数签名完全复用**：保留原有函数名、枚举、宏命名，只在类型系统所需处添加 C++ 特有限定（如 namespace、const 引用）。
- **执行流程逐语句对齐**：按照 JS 源码顺序重写，必要时通过注释标记“与 React vX.Y 源码行号对应”。
- **Feature Flag 全局一致**：所有编译期/运行期的 feature flag、常量定义均复用 `shared/ReactFeatureFlags.js` 的值与命名。
- **禁止随意重构**：仅当 JS 端依赖原生 API（如 `Object.assign`）时，才封装最小 C++ 等价实现。

### 统一翻译工作流
1. **基准版本锁定**：通过 `react-main` 子模块或固定 tag（当前：`main@<commit>`），在翻译开始前冻结。
2. **模版生成**：使用脚本读取 JS AST，根据函数/常量导出生成 C++ 头/源文件骨架。
3. **语义复刻**：以 JS 源码为右屏，逐语句翻译为 C++，优先保持控制流与变量命名。
4. **行为校验**：同步扩写 gtest + Wasm 端到端用例，确保与 `react-main` 对应单测的行为一致。
5. **差异快照**：借助自研 `translate-react.js`（待实现）输出 JS/C++ AST 对比报告，构建 CI 守护。

## 1. 模块映射矩阵（示例节选）

| `react-main` 源码路径 | C++ 目标文件 | 翻译策略备注 |
| --- | --- | --- |
| `packages/react-reconciler/src/ReactFiberWorkLoop.new.js` | `packages/ReactCpp/src/reconciler/ReactFiberWorkLoop.cpp` | 保留同名函数；`performUnitOfWork` 等逻辑逐行翻译，借助 lambda 规避闭包差异。 |
| `packages/react-reconciler/src/ReactFiberWorkLoop.shared.js` | `packages/ReactCpp/src/reconciler/ReactFiberWorkLoopShared.cpp` | Feature flag 常量放入 `ReactFeatureFlags.h`，导出 API 保持一致。 |
| `packages/react-dom/src/client/ReactDOMHostConfig.js` | `packages/ReactCpp/src/react-dom/client/ReactDOMHostConfig.cpp` | DOM host 操作映射到 `ReactDOMInstance`，事件配置保持原 key。 |
| `packages/react-dom/src/client/ReactDOMComponent.js` | `packages/ReactCpp/src/react-dom/client/ReactDOMComponent.cpp` | `diffProperties` 逻辑与 `commitUpdate` payload 逐项对齐。 |
| `packages/shared/ReactFeatureFlags.js` | `packages/ReactCpp/src/shared/ReactFeatureFlags.cpp` | 通过自动化生成器同步常量，支持多构建配置。 |
| `packages/scheduler/src/forks/Scheduler.js` | `packages/ReactCpp/src/scheduler/Scheduler.cpp` | 使用同名优先级枚举，事件Loop策略与 JS 端相同。 |
| `packages/react/src/ReactHooks.js` | `packages/ReactCpp/src/react/hooks/ReactHooks.cpp` | Dispatcher 模式复刻，Hook slot 结构体与 JS `memoizedState` 对齐。 |
| `packages/react-dom/src/events/DOMPluginEventSystem.js` | `packages/ReactCpp/src/react-dom/events/DOMPluginEventSystem.cpp` | 构建统一事件注册表，名称与分发路径保持一致。 |

> 完整映射详见 `docs/matrix/react-source-mapping.csv`（Phase 0 交付物）。

## 2. 阶段总览

| 阶段 | 主题 | 核心范围 | 状态 | 测试数 | 目标完成时间 |
| --- | --- | --- | --- | --- | --- |
| Phase 0 | 源码镜像 & Flag 清点 | 目录映射、模板生成、差异报告工具 | ✅ 已完成 | - | 2025-10-20 |
| Phase 1 | Shared/Feature Scaffold | Feature flags、共享常量、错误码对齐 | ✅ 已完成 | 45 | 2025-10-31 |
| Phase 2 | ReactDOM Host Parity | `ReactDOMHostConfig`、`ReactDOMInstance`、属性 diff | ✅ 已完成 | 65 | 2025-11-15 |
| Phase 3 | Fiber 数据结构 | `FiberNode`、`FiberRootNode`、UpdateQueue | ✅ 已完成 | 95 | 2025-11-29 |
| Phase 4 | WorkLoop & Commit (Sync) | `beginWork`/`completeWork`/`commit*` 同构 | ✅ 已完成 | 120 | 2025-12-20 |
| Phase 5 | Scheduler 集成 | `ensureRootScheduled` 与调度器 1:1 | ✅ 已完成 | 145 | 2025-12-25 |
| Phase 6 | Suspense & Thenable | Suspense 组件、Promise 处理、并发更新 | ✅ 已完成 | 170 | 2025-12-26 |
| Phase 7 | Hooks & Context | Hook dispatcher、Context 注册、Effect queue | ✅ 已完成 | 193 | 2025-12-27 |
| Phase 8 | 错误处理与边界 | CapturedValue、Throw、Unwind、Error Boundary | ✅ 已完成 | 225 | 2025-12-27 |
| Phase 9 | BeginWork/CompleteWork 测试 | 渲染流程验证、bubbleProperties、bailout | ✅ 已完成 | 249 | 2025-12-27 |
| Phase 10 | ReactChildFiber 子节点协调 | Diff 算法、删除标记、位置检测、Fiber 复用 | ✅ 已完成 | 298 | 2025-12-28 |
| Phase 11 | ReactFiberClassUpdateQueue | 更新队列管理、状态计算、回调处理 | ✅ 已完成 | 354 | 2025-12-28 |
| Phase 12 | 官方测试 & 兼容性 | Jest 子集、双端 snapshot、CI 验证 | ⚪ 未开始 | - | 2026-Q1 |
| Phase 13 | Wasm 产线 & 调优 | cheap toolchain、浏览器装载、性能基准 | ⚪ 未开始 | - | 2026-Q2 |

> 若 `react-main` upstream 有 breaking 变更，将回滚到锁定 tag，并在 Phase 0 工具中记录差异。

## 3. 阶段详解

### Phase 0 · 源码镜像 & Flag 清点（进行中）

**目标**：建立从 JS 源到 C++ 源的一致性保障，确保每一次 commit 能够确认翻译范围与差异。

**关键交付**
- `scripts/translate-react.js`：读取 JS AST，输出 C++ 头/源模板（包含 namespace、函数声明、TODO 注释），并生成同名 `.expect.json` AST 描述。（已落地）
- `docs/matrix/react-source-mapping.csv`：列出每个 JS 文件的 C++ 对应路径与负责人。（初版已生成）
- `ci/react-parity-report.md`：每日 CI 产物，展示「已翻译 JS 行数 / 总行数」「存在偏差的函数列表」（初版报告脚本已上线）。
- `vendor/react-main/`：固定 upstream mirror（当前以 symlink 指向本地 checkout，可替换为子模块或镜像仓库）。
- Feature flag 清单：`packages/ReactCpp/src/shared/ReactFeatureFlags.h` 自动生成，支持 DEV / PROD / EXP builds。

**任务现状**
- [x] 输出《CppReactArchitecture》骨架文档，梳理各模块职责。
- [x] 对齐初版 `ReactDOMInstance` API，确保宿主接口有桩。
- [x] 实现 `translate-react.js` AST 模板导出。
- [x] 输出 `docs/matrix/react-source-mapping.csv` 初版矩阵。
- [x] 编写 `scripts/check-parity.js`，比较 JS/C++ AST 并报出缺失函数。
- [x] 生成 `ci/react-parity-report.md` Markdown 报告入口。
- [x] 将 `react-main` 作为 git 子模块或 mirror，引入 `vendor/react-main/`。
- [ ] 生成 Feature Flag 自动化 pipeline（JS ➜ JSON ➜ C++ header）。

**验收标准**
- 任意 `packages/react-*/src/*.js` 在映射表中都有唯一 C++ 目标文件。
- CI parity 报告无 404/跳过项。
- Feature flag header 与 JS 端的 `__EXPERIMENTAL__` 值完全一致。

### Phase 1 · Shared/Feature Scaffold（进行中）

**目标**：翻译所有共享模块，确保 Reconciler 依赖的常量、错误信息、工具函数与 JS 同步。

**关键交付**
- `packages/ReactCpp/src/shared/` 下的 `ReactFeatureFlags.cpp/h`, `ReactWorkTags.cpp/h`, `ReactFiberFlags.cpp/h` 等文件，通过脚本生成或手工翻译。
- `shared/ReactErrorUtils.js` ➜ `ReactErrorUtils.cpp`，保留同名 API。
- 建立 `SharedRuntimeTests`：验证常量值、flag 切换效果与 JS 端 snapshot 对齐。

**任务清单**
- [x] 翻译 `shared/ReactWorkTags.js` 与 `shared/ReactFiberFlags.js`。
- [x] 建立 `enum class WorkTag` 与 `Flags`，并提供 `constexpr` 映射表。
- [x] 翻译 `shared/ReactFeatureFlags.js`，新增 `REACTCPP_ENABLE_EXPERIMENTAL` / `REACTCPP_ENABLE_PROFILE` 宏支撑多构建配置。
- [x] 翻译 `shared/ReactOwnerStackReset.js`，与 `ReactSharedInternals` runtime 状态保持一致。
- [ ] 引入 `packages/shared/ReactSideEffectTags` ➜ C++ 常量。
- [x] 翻译 `packages/shared/ReactSymbols.js`、`ReactSharedInternals.js`，统一导出 symbol & dispatcher 常量。
- [x] 构建 gtest 保障——确保 `ReactWorkTags`、`ReactFiberFlags`、`ReactFeatureFlags` 数值与 JS 快照一致（新增 `ReactSharedConstantsTests.cpp`）。

**验收标准**
- C++ 端常量与 JS snapshot 一致（CI 对比 JSON）。
- 所有共享模块被 WorkLoop 与 HostConfig 成功引用。

### Phase 2 · ReactDOM Host Parity（未开始）

**目标**：确保宿主配置层完全一致，便于后续 WorkLoop 直接复用。

**关键交付**
- `ReactDOMHostConfig.cpp/h`、`ReactDOMInstance.cpp/h`、`ReactDOMComponent.cpp/h` 的 1:1 翻译。
- `ReactDOMDiffProperties.cpp`：属性 diff 与事件处理逻辑逐语句对齐（已在进行中，后续纳入 parity 检查）。
- `HostStubRuntime` gtest 桩，实现 append/remove/insert 与属性更新校验。

**任务清单**
- [ ] 使用模板生成器创建 C++ 框架，补全 `prepareUpdate` / `commitUpdate` / `commitTextUpdate` 等函数。
- [ ] 翻译 `setValueForProperty` / `dangerousStyleValue` 等辅助逻辑。
- [ ] 将事件寄存系统 `ReactDOMEventListener.js` 转写为 C++，保留 key 大小写。
- [ ] 扩展 `ReactDOMComponentTests`，参照 React 官方 `__tests__/ReactDOMComponent-test.js`。
- [x] （进行中）基于 `ReactHostInterface` 搭建 DOM 宿主桩，验证 `performance` / `console` 注入路径与 `ReactDOMHostConfig` 的交互（新增 host log 测试）。

**验收标准**
- Host 桩测试覆盖 append/remove/insertBefore/属性 diff/事件绑定。
- parity 报告显示 `ReactDOMHostConfig` 与 `ReactDOMComponent` 无遗漏函数。

### Phase 3 · Fiber 数据结构（进行中）

**目标**：复刻 Fiber 节点、更新队列、Lane 模型，为 WorkLoop 做准备。

**关键交付**
- `FiberNode.h`, `FiberRootNode.h`, `Lane.cpp/h`，所有字段命名与 JS `FiberNode.js` 一致。
- `UpdateQueue.cpp/h`：维护 `sharedQueue`, `effectTag` 等属性。

**进展速记**
- ✅ 使用 `translate-react.js` 生成 `FiberNode.h/.cpp` 与 `FiberRootNode.h/.cpp` 模板骨架，并补齐命名空间、注释衔接 React JS 源码行号。
- ✅ 输出 `ReactFiberLane.h` Lane mask 常量与基础 helper，实现 `NoLane` ➜ `DeferredLane` 全量位掩码对齐，并新增静态断言快照。
- ✅ 拓展 `FiberRoot` 状态（纠缠、Indicator、到期位图）并落地 `markRootUpdated`、`getEntangledLanes` 等核心入口，运行时断言验证。
- ✅ 引入 `LanePriority` 映射与到期策略（`computeExpirationTime`、`markStarvedLanesAsExpired`），同步补充运行时断言。
- ✅ 首批 `UpdateQueue` 单元测试：验证 `enqueueUpdate` 环状拼接、`appendPendingUpdates` 拆圈、`processUpdateQueue` 状态推进与回调收集。
- ✅ 补全集合型 FiberRoot helper：实现 `markRootSuspended`/`markRootPinged`/`markRootEntangled` 以及 Deferred lane 产生活跃路径，配套运行时测试覆盖。
- ✅ 继续下沉 FiberRoot 完成路径：翻译 `markRootFinished`、`upgradePendingLanesToSync`、`markHiddenUpdate` 以及 hydration lane bump 逻辑，并拓展断言用例验证隐藏更新与重试 lane 状态。
- ✅ 扩展 updater 追踪支撑：为 `FiberRoot` 增补 `pendingUpdatersLaneMap`/`memoizedUpdaters` 并落地 `addFiberToLanesMap`、`movePendingFibersToMemoized` 桩实现，为后续 DevTools 联动预留接口。
- ✅ 补全 transition lane 桥接：引入 `transitionLanes` 结构及 `addTransitionToLanesMap`/`getTransitionsForLanes`/`clearTransitionsForLanes`，在 flag 关闭场景下保持零开销，便于后续启用 Transition Tracing。
- ✅ 首版 `ReactFiberConcurrentUpdates` 翻译：落地并串联并发更新排队、悬挂更新继承、隐藏更新标记逻辑，配套运行时用例覆盖基本 enqueue/flush 行为。
- ✅ 同步 `FiberNode` 依赖克隆与重置语义：引入 `Dependencies` 结构体与 `createWorkInProgress`/`resetWorkInProgress` 断言覆盖，确保双缓冲 fiber 不共享上下文快照。
- ✅ 接入 FiberRoot 错误回调：对齐 `logUncaughtError` / `logCaughtError` 行为，并提供默认全局上报兜底。
- ✅ 复刻 `enqueueConcurrentHookUpdateAndEagerlyBailout` 条件刷新逻辑，与 JS 并行队列排队语义保持一致。

**任务清单**
- [x] 生成 `FiberNode.h/.cpp`、`FiberRootNode.h/.cpp` 模板文件，保持字段与构造逻辑签名一致。
- [x] 搭建 `ReactFiberLane.h` 常量/工具集（含 `SyncUpdateLanes`, `HydrationLanes` 等），配套 `ReactFiberLaneTests.cpp` 静态断言。
- [x] 翻译 `ReactFiberConcurrentUpdates.js` 核心入口，接入 FiberRoot/Lane helper 并补充运行时断言。
- [ ] 将 `packages/react-reconciler/src/ReactFiberClassComponent.js` 的更新流程翻译到 C++ `UpdateQueue`，覆盖 `enqueueUpdate` / `processUpdateQueue` 路径。
- [x] 引入 `LanePriority` 数值表及到期策略，补全 Lane 相关辅助函数链路，并与 Feature Flag 生成流程打通（DEV/PROD 同步校验）。
- [ ] 构建 gtest：验证 `createFiber`, `createFiberFromElement`, `enqueueUpdate`，确保与 JS 快照一致。（进行中：已新增 `ReactFiberRuntimeTests` 覆盖 `createWorkInProgress`/`resetWorkInProgress` 依赖复制与还原逻辑）

**验收标准**
- 结构体字段顺序与 JS 端 `FiberNode` 注释对应。
- 测试覆盖基本的节点创建与更新排队。

### Phase 4 · WorkLoop & Commit (Sync)（进行中）

**目标**：完成同步工作循环与提交阶段的逐行翻译。

**关键交付**

**任务清单**
- ✅ 追加 `panicOnRootError`、`completeUnitOfWork`、`unwindUnitOfWork`、`performUnitOfWork`、`workLoopSync`/`workLoopConcurrent` 驱动与 `renderRootSync`/`renderRootConcurrent` 雏形（当前搭载 Profiler/BeginWork 桩），并在 `ReactFiberWorkLoopStateTests` 中验证 WIP 指针及 Root Exit 状态。
- [ ] 使用 AST 工具从 `ReactFiberBeginWork.new.js`, `ReactFiberCompleteWork.new.js`, `ReactFiberCommitWork.new.js` 自动生成 C++ 骨架。
- [ ] 迁移 `ChildReconciler`（`ReactChildFiber.js`）逻辑，保留 key diff 行为。
- [ ] 建立 `ReactFiberWorkLoopTests`：渲染 `<div><p>Hello</p></div>`、更新 props、删除节点。
- [ ] 实现 `resetSuspendedWorkLoopOnUnwind` / `unwindInterruptedWork` 具体逻辑，恢复上下文与 Hook 栈状态。

**验收标准**
- `renderRootSync` 在 C++ 端构建 fiber 树并驱动 host 节点。
- 所有渲染单测与 JS 端 snapshot 一致。

### Phase 5 · Scheduler 集成（未开始）

**目标**：引入时间切片，使用同名优先级枚举与任务模型。

**关键交付**
- `Scheduler.cpp/h` 翻译 `packages/scheduler/src/forks/Scheduler.js`（考虑 host 环境差异）。
- `MessageChannel` 模拟器（必要时使用 libuv/cheap event loop 适配）。

**任务清单**
- [ ] 翻译 `requestHostCallback`, `flushWork`, `advanceTimers` 等函数。
- [ ] 将 `ensureRootScheduled` 切换到调度器驱动。
- [ ] 新增单测：多任务优先级、过期任务抢占。

**验收标准**
- 调度器单测与 React 官方 scheduler 测试输出一致。
- parity 报告显示 scheduler 文件全部翻译。

### Phase 6 · Hydration & 事件系统（未开始）

**目标**：复刻 SSR Hydration 流程与 DOM 事件系统。

**关键交付**
- `ReactFiberHydrationContext.cpp`, `ReactDOMEventListener.cpp`。
- Wasm Hydration 桥接：从 JS 提供的真实 DOM 节点引用进行匹配。

**任务清单**
- [ ] 翻译 `ReactFiberHydrationContext.new.js`。
- [ ] 将 DOM Plugin System 的事件优先级、冒泡、捕获完整复刻。
- [ ] 增加 `HydrationTests`: 成功/失败/恢复路径。

**验收标准**
- 与 React 官方 `ReactDOMServerIntegration` 子集一致。
- Hydration 失败走 fallback 渲染路径，行为与 JS 对齐。

### Phase 7 · Hooks & Context（✅ 已完成）

**目标**：实现 Hook dispatcher、Context 注册等高级特性。

**关键交付**
- ✅ `ReactFiberHooksImpl.h` - 完整 Hooks 实现（1200+ 行）
- ✅ `objectIs.h` - Object.is 比较工具
- ✅ `ReactFiberNewContext.h` - Context 系统实现
- ✅ `ReactHooksTests.cpp` - 全面测试覆盖（40+ 测试）
- ✅ `ReactFiberSuspenseComponent.h` - Suspense 组件实现
- ✅ `ReactFiberThenable.h` - Thenable/Promise 处理
- ✅ `ReactFiberSuspenseContext.h` - Suspense 上下文
- ✅ `ReactFiberConcurrentUpdates.h` - 并发更新队列

**任务清单**
- [x] 翻译 `useState`, `useReducer`, `useEffect`, `useLayoutEffect` 等实现。
- [x] 处理 `mount`/`update` 双分支。
- [x] gtest 覆盖状态更新、Effect 调度。
- [x] 实现 useMemo, useCallback, useRef, useContext
- [x] 实现 useTransition, useDeferredValue, useId
- [x] 实现 Dispatcher 结构与切换机制
- [x] 实现 Suspense 组件状态与边界处理
- [x] 实现 Thenable 追踪与恢复机制

**测试统计**: 193 个测试通过

**验收标准**
- Hooks 测试（C++ 端）与 JS fixtures 输出一致。
- Hooks dispatch 结构与 JS `currentHook` 链条对齐。

### Phase 8 · 错误处理与边界（✅ 已完成）

**目标**：实现错误边界、异常捕获与 Fiber 栈展开机制。

**关键交付**
- ✅ `ReactCapturedValue.h` - 捕获值类型系统
- ✅ `ReactFiberThrow.h` - 错误抛出处理
- ✅ `ReactFiberUnwindWork.h` - Fiber 栈展开
- ✅ `ReactErrorHandlingTests.cpp` - 错误处理测试（32 个测试）

**实现详情**
| C++ 类型/函数 | JS 源函数 | 源文件 |
|--------------|----------|--------|
| `CapturedValue<T>` | `CapturedValue` | ReactCapturedValue.js |
| `ErrorCapturedValue` | `createCapturedValueAtFiber` | ReactCapturedValue.js:30-60 |
| `FiberUpdate<State>` | `Update` | ReactFiberClassUpdateQueue.js |
| `createRootErrorUpdate` | `createRootErrorUpdate` | ReactFiberThrow.js:88-105 |
| `createClassErrorUpdate` | `createClassErrorUpdate` | ReactFiberThrow.js:107-109 |
| `throwException` | `throwException` | ReactFiberThrow.js:111-235 |
| `markSuspenseBoundaryShouldCapture` | `markSuspenseBoundaryShouldCapture` | ReactFiberThrow.js:236-310 |
| `unwindWork` | `unwindWork` | ReactFiberUnwindWork.js:92-200 |
| `unwindInterruptedWork` | `unwindInterruptedWork` | ReactFiberUnwindWork.js:202-290 |
| `completeUnitOfUnwind` | `completeUnitOfUnwind` | ReactFiberUnwindWork.js:292-320 |

**任务清单**
- [x] 翻译 `ReactCapturedValue.js` - 泛型捕获值模板
- [x] 翻译 `ReactFiberThrow.js` - 错误/Suspense 抛出处理
- [x] 翻译 `ReactFiberUnwindWork.js` - 栈展开与上下文恢复
- [x] 实现 Legacy Error Boundary 追踪
- [x] 实现 Suspense 边界标记与捕获

**测试统计**: 225 个测试通过（+32）

**验收标准**
- ✅ 错误边界正确捕获子树错误
- ✅ Suspense 边界正确处理 Promise
- ✅ 栈展开正确恢复上下文状态

### Phase 9 · BeginWork/CompleteWork 测试（✅ 已完成）

**目标**：验证渲染阶段核心函数的正确性。

**关键交付**
- ✅ `ReactBeginCompleteWorkTests.cpp` - 渲染流程测试（24 个测试）
- ✅ 验证 `ReactFiberBeginWork.h` (550 行)
- ✅ 验证 `ReactFiberCompleteWork.h` (493 行)

**测试覆盖**
- BeginWorkContext 状态管理
- ChildReconciler 结构验证
- BubbleProperties 逻辑测试
- Fiber 类型完成处理
- Bailout 优化路径
- 树遍历与 alternate 交换

**测试统计**: 249 个测试通过（+24）

**验收标准**
- ✅ 所有 Fiber 类型的 begin/complete 路径有测试覆盖
- ✅ bubbleProperties 正确合并 flags 和 lanes
- ✅ bailout 优化路径正确判断

### Phase 10 · ReactChildFiber 子节点协调（✅ 已完成）

**目标**：实现 React 核心 Diff 算法，完成子节点协调、删除标记、位置检测与 Fiber 复用机制。

**关键交付**
- ✅ `ReactChildFiber.h` - 完整 Child Reconciler 实现（884 行）
- ✅ `ReactChildFiberTests.cpp` - 全面测试覆盖（49 个测试）

**实现详情**
| C++ 类型/函数 | JS 源函数 | 源文件 |
|--------------|----------|--------|
| `ReactChildFiberReconciler` | `ChildReconciler` 工厂闭包 | ReactChildFiber.js:130-2000 |
| `reconcileChildFibers()` | `reconcileChildFibers` | ReactChildFiber.js:1950-2050 |
| `reconcileChildrenArray()` | `reconcileChildrenArray` | ReactChildFiber.js:750-950 |
| `reconcileSingleElement()` | `reconcileSingleElement` | ReactChildFiber.js:1200-1350 |
| `reconcileSingleTextNode()` | `reconcileSingleTextNode` | ReactChildFiber.js:1100-1150 |
| `reconcileSinglePortal()` | `reconcileSinglePortal` | ReactChildFiber.js:1350-1450 |
| `deleteChild()` | `deleteChild` | ReactChildFiber.js:350-400 |
| `deleteRemainingChildren()` | `deleteRemainingChildren` | ReactChildFiber.js:400-450 |
| `placeChild()` | `placeChild` | ReactChildFiber.js:500-550 |
| `updateSlot()` | `updateSlot` | ReactChildFiber.js:600-700 |
| `updateFromMap()` | `updateFromMap` | ReactChildFiber.js:700-750 |
| `mapRemainingChildren()` | `mapRemainingChildren` | ReactChildFiber.js:550-600 |
| `useFiber()` | `useFiber` | ReactChildFiber.js:250-300 |
| `createChild()` | `createChild` | ReactChildFiber.js:450-500 |
| `placeSingleChild()` | `placeSingleChild` | ReactChildFiber.js:2060-2080 |
| `cloneChildFibers()` | `cloneChildFibers` | ReactChildFiber.js:2085-2115 |
| `resetChildReconcilerOnUnwind()` | `resetChildReconcilerOnUnwind` | ReactChildFiber.js:2117 |

**核心算法实现**
- **单节点协调**：`reconcileSingleElement`、`reconcileSingleTextNode`、`reconcileSinglePortal` 处理单个子节点的复用或创建
- **数组协调（Diff 算法）**：`reconcileChildrenArray` 实现两轮扫描 - 首轮线性匹配，二轮使用 key-map 查找
- **删除标记**：`deleteChild` 将待删除 Fiber 链入父节点的 `deletions` 列表，设置 `ChildDeletion` flag
- **位置检测**：`placeChild` 比较 `oldIndex` 与 `lastPlacedIndex`，检测是否需要移动
- **Fiber 复用**：`useFiber` 克隆已有 Fiber 避免重新创建

**任务清单**
- [x] 翻译 `ReactChildFiber.js` 核心类 `ChildReconciler`
- [x] 实现单节点协调：Element、TextNode、Portal
- [x] 实现数组协调 Diff 算法（两轮扫描）
- [x] 实现删除标记与 deletions 列表管理
- [x] 实现位置检测与 Placement flag 设置
- [x] 实现 Fiber 复用与克隆机制
- [x] 实现 `cloneChildFibers` 批量克隆
- [x] gtest 覆盖所有协调路径

**测试统计**: 298 个测试通过（+49）

**测试覆盖**
- ChildFiberReconcilerTest：基础配置测试
- ChildFiberDeletionTest：删除标记逻辑
- ChildFiberSingleNodeTest：单节点协调
- ChildFiberArrayTest：数组 Diff 算法
- ChildFiberPlacementTest：位置检测与移动
- ChildFiberReuseTest：Fiber 复用机制
- ChildFiberMapTest：Key-Map 构建与查找
- ChildFiberTextUpdateTest：文本节点更新
- ChildFiberReconcileAPITest：公共 API 接口
- ChildFiberHelperTest：辅助函数测试
- ChildFiberEdgeCaseTest：边界情况处理
- ChildFiberCreateChildTest：子节点创建
- PlaceSingleChildHelperTest：单子节点放置
- CloneChildFibersTest：批量克隆测试

**验收标准**
- ✅ 单节点协调正确复用已有 Fiber
- ✅ 数组 Diff 正确检测新增、删除、移动操作
- ✅ 删除标记正确链入 deletions 列表
- ✅ Placement flag 正确标记需移动节点

### Phase 11 · ReactFiberClassUpdateQueue（✅ 已完成）

**目标**：实现 React 类组件的更新队列管理系统，支持状态更新、强制更新和回调处理。

**关键交付**
- ✅ `ReactFiberClassUpdateQueue.h` - 完整的 UpdateQueue 实现（720 行）
- ✅ `ReactFiberClassUpdateQueueTests.cpp` - 全面测试覆盖（61 个测试）

**实现详情**
| C++ 类型/函数 | JS 源函数 | 源文件 |
|--------------|----------|--------|
| `UpdateTag` | `UpdateState/ReplaceState/ForceUpdate/CaptureUpdate` | ReactFiberClassUpdateQueue.js:155-158 |
| `ClassUpdate<State>` | `Update` 类型 | ReactFiberClassUpdateQueue.js:127-138 |
| `ClassSharedQueue<State>` | `SharedQueue` 类型 | ReactFiberClassUpdateQueue.js:140-144 |
| `ClassUpdateQueue<State>` | `UpdateQueue` 类型 | ReactFiberClassUpdateQueue.js:146-153 |
| `initializeClassUpdateQueue()` | `initializeUpdateQueue` | ReactFiberClassUpdateQueue.js:176-185 |
| `cloneClassUpdateQueue()` | `cloneUpdateQueue` | ReactFiberClassUpdateQueue.js:187-205 |
| `createClassUpdate()` | `createUpdate` | ReactFiberClassUpdateQueue.js:162-172 |
| `enqueueClassUpdate()` | `enqueueUpdate` | ReactFiberClassUpdateQueue.js:217-241 |
| `enqueueClassCapturedUpdate()` | `enqueueCapturedUpdate` | ReactFiberClassUpdateQueue.js:243-290 |
| `processClassUpdateQueue()` | `processUpdateQueue` | ReactFiberClassUpdateQueue.js:380-580 |
| `getStateFromClassUpdate()` | `getStateFromUpdate` | ReactFiberClassUpdateQueue.js:307-378 |
| `commitClassCallbacks()` | `commitCallbacks` | ReactFiberClassUpdateQueue.js:594-618 |
| `commitHiddenClassCallbacks()` | `commitHiddenCallbacks` | ReactFiberClassUpdateQueue.js:620-640 |
| `deferHiddenClassCallbacks()` | `deferHiddenCallbacks` | ReactFiberClassUpdateQueue.js:640-660 |

**核心机制实现**
- **更新类型**：`UpdateTag` 枚举支持 `UpdateState`、`ReplaceState`、`ForceUpdate`、`CaptureUpdate` 四种更新类型
- **循环链表结构**：`ClassSharedQueue::pending` 维护待处理更新的循环链表，支持 O(1) 入队
- **状态计算**：`getStateFromClassUpdate` 根据 payload 类型（值或函数）计算新状态
- **队列处理**：`processClassUpdateQueue` 遍历所有更新，合并状态，处理优先级跳过
- **回调管理**：更新完成后收集并执行回调函数
- **Lanes 集成**：与 React Lanes 调度系统完全集成，支持优先级更新

**任务清单**
- [x] 翻译 `UpdateTag` 枚举和常量
- [x] 实现 `ClassUpdate<State>` 模板结构
- [x] 实现 `ClassSharedQueue<State>` 共享队列
- [x] 实现 `ClassUpdateQueue<State>` 完整队列结构
- [x] 实现 `ClassUpdateQueueGlobals` 全局状态单例
- [x] 实现 `initializeClassUpdateQueue()` 队列初始化
- [x] 实现 `cloneClassUpdateQueue()` 队列克隆
- [x] 实现 `createClassUpdate()` 更新创建
- [x] 实现 `enqueueClassUpdate()` 更新入队
- [x] 实现 `enqueueClassCapturedUpdate()` 错误边界更新
- [x] 实现 `processClassUpdateQueue()` 队列处理
- [x] 实现 `getStateFromClassUpdate()` 状态计算
- [x] 实现回调处理函数
- [x] gtest 覆盖所有更新路径

**测试统计**: 354 个测试通过（+56）

**测试覆盖**
- ClassUpdateTagTest：UpdateTag 枚举值验证
- ClassUpdateStructTest：ClassUpdate 结构测试
- ClassSharedQueueTest：共享队列测试
- ClassUpdateQueueTest：完整队列测试
- ClassUpdateQueueGlobalsTest：全局状态测试
- InitializeClassUpdateQueueTest：队列初始化
- CreateClassUpdateTest：更新创建
- CloneClassUpdateQueueTest：队列克隆
- EnqueueClassUpdateTest：更新入队
- GetStateFromClassUpdateTest：状态计算
- ClassForceUpdateTest：强制更新
- ClassCallbackTest：回调处理
- ClassHelperFunctionsTest：辅助函数
- EnqueueClassCapturedUpdateTest：捕获更新
- ClassTypeAliasTest：类型别名
- ClassEdgeCaseTest：边界情况
- ClassScheduleUpdateTest：调度更新

**验收标准**
- ✅ 更新正确入队并维护循环链表结构
- ✅ 状态计算支持值更新和函数更新
- ✅ ForceUpdate 正确设置标志
- ✅ CaptureUpdate 正确处理错误边界
- ✅ 回调在更新完成后正确执行
- ✅ 与 ReactFiberThrow 的类型定义兼容

### Phase 12 · 官方测试 & 兼容性（未开始）

**目标**：在 Jest 环境运行 React 官方测试子集，确保行为一致。

**关键交付**
- Jest runner 集成 Wasm runtime，JS ➜ C++ 调用桥。
- `tests/react/fixtures/` 对齐：对每个选定测试生成 Wasm 版执行脚本。
- CI pipeline：`npm test -- react-dom/...` & `ctest` 组合。

**验收标准**
- 至少 30% 官方测试子集通过，持续提升覆盖。
- CI parity 报告无新增偏差。

### Phase 13 · Wasm 产线 & 调优（未开始）

**目标**：构建生产级 Wasm 构建与性能调优工具链。

**关键交付**
- cheap toolchain 集成、Wasm loader。
- 浏览器 demo：使用 React 官方 fixture，比较 JS vs Wasm。
- Benchmark 报告：`bin/run-benchmarks.py` 对接 C++ runtime。

**验收标准**
- 浏览器 demo 可运行 `<ConcurrentModeApp />`。
- 性能指标与 JS baseline 对比报告。

## 4. JS ➜ C++ 机械翻译流水线

1. **源文件检索**：`scripts/scan-react.js` 读取 `react-main` 目录，过滤 `.js`/`.jsx`（排除测试）。
2. **AST 解析**：使用 Babel parser 输出带位置信息的 JSON。
3. **C++ 模板生成**：按 export 名称输出 `.h/.cpp` 模板，包含 TODO 注释与原行号。
4. **翻译清单**：将待翻译函数列入 `translation-status.json`，标记 Responsible/Reviewer。
5. **实现阶段**：贡献者在模板内补充 C++ 实现，并在注释中保留原 JS 行号。
6. **自动对比**：CI 执行 `scripts/check-parity.js`，验证函数签名、控制流结构（if/while/switch）一致。
7. **行为验证**：运行对应 gtest/Jest fixture；CI 对比产出的日志/Hydration diff。
8. **文档更新**：翻译完成后更新映射 CSV 与阶段进度表。

> 所有脚本成果在 Phase 0/1 内完成并纳入 CI。

## 5. 近期迭代（Sprint：2025-12-20 ~ 2025-12-27）

| Task | Owner | 状态 | 说明 |
| --- | --- | --- | --- |
| Phase 7: Suspense 组件实现 | 平台组 | ✅ 已完成 | `ReactFiberSuspenseComponent.h` + 测试 |
| Phase 7: Thenable 处理 | 平台组 | ✅ 已完成 | `ReactFiberThenable.h` Promise 追踪 |
| Phase 7: Suspense Context | 平台组 | ✅ 已完成 | `ReactFiberSuspenseContext.h` 上下文栈 |
| Phase 7: 并发更新队列 | 平台组 | ✅ 已完成 | `ReactFiberConcurrentUpdates.h` |
| Phase 7: Suspense 测试 | QA 组 | ✅ 已完成 | 42 个测试，总计 193 通过 |
| Phase 8: CapturedValue 实现 | 平台组 | ✅ 已完成 | `ReactCapturedValue.h` 泛型捕获值 |
| Phase 8: Throw 处理 | 平台组 | ✅ 已完成 | `ReactFiberThrow.h` 错误/Suspense 抛出 |
| Phase 8: Unwind 实现 | 平台组 | ✅ 已完成 | `ReactFiberUnwindWork.h` 栈展开 |
| Phase 8: 错误处理测试 | QA 组 | ✅ 已完成 | 32 个测试，总计 225 通过 |
| Phase 9: BeginWork 测试 | 平台组 | ✅ 已完成 | 验证渲染开始阶段 |
| Phase 9: CompleteWork 测试 | 平台组 | ✅ 已完成 | 验证渲染完成阶段 |
| Phase 9: BubbleProperties 测试 | QA 组 | ✅ 已完成 | 24 个测试，总计 249 通过 |
| Phase 10: ReactChildFiber 实现 | 平台组 | ✅ 已完成 | `ReactChildFiber.h` 完整 Diff 算法 |
| Phase 10: ChildFiber 测试 | QA 组 | ✅ 已完成 | 49 个测试，总计 298 通过 |
| Phase 11: ClassUpdateQueue 实现 | 平台组 | ✅ 已完成 | `ReactFiberClassUpdateQueue.h` 更新队列 |
| Phase 11: ClassUpdateQueue 测试 | QA 组 | ✅ 已完成 | 56 个测试，总计 354 通过 |

### 测试进度统计

| 日期 | 测试总数 | 新增 | 里程碑 |
| --- | --- | --- | --- |
| 2025-12-25 | 151 | - | Phase 0-6 完成 |
| 2025-12-26 | 193 | +42 | Phase 7 Suspense 完成 |
| 2025-12-27 | 225 | +32 | Phase 8 错误处理完成 |
| 2025-12-27 | 249 | +24 | Phase 9 渲染测试完成 |
| 2025-12-28 | 298 | +49 | Phase 10 子节点协调完成 |
| 2025-12-28 | 354 | +56 | Phase 11 ClassUpdateQueue 完成 |

每日站会需更新 AST 翻译覆盖率 & 测试通过率。

## 6. 风险与应对

- **上游变更频繁**：需维护 react-main mirror 的 `CHANGELOG`，通过 parity 报告提示新增/删除函数。
- **JS 内建 API 差异**：如 `Object.is`、`Map` 等，需统一封装在 `shared/JSMimics.cpp`，谨防重复实现。
- **Hydration DOM 依赖**：浏览器环境与测试环境 API 不一致；需在 Phase 6 前定义 Wasm DOM 代理协议。
- **性能回归风险**：逐行翻译可能带来 C++ 性能损失，Phase 9 引入 profile 工具，确保追加优化不破坏一致性。

## 7. 参考资料

- `vendor/react-main`（锁定 tag 待补充）
- [React Architecture Docs](https://react.dev/learn/render-and-commit)
- 项目内部文档：`packages/ReactCpp/docs/CppReactArchitecture.md`
- 相关脚本（待补充）：`scripts/translate-react.js`, `scripts/check-parity.js`

---

> 文档维护：平台组。每周五更新 parity 指标，或当阶段完成时即时刷新。
