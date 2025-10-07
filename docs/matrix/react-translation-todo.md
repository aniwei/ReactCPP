# React Translation TODO Tracker

本文件用于跟踪 `react-main` 源码到 `ReactCPP` C++ 复刻的逐函数、逐表达式映射状态，作为后续迭代的权威 TODO 列表。

## 使用约定

- 每个条目均对应 `reactjs/` 下的 JS 源文件与 C++ 对应实现。
- 状态列含义：
  - ✅ 已逐行验证，表达式与 JS 保持一致。
  - 🔄 部分完成，具体缺口在备注或子清单中列出。
  - ⛔ 未开始，尚无对应实现。
- 子清单用复选框列出需要逐项核对的表达式 / 分支 / 副作用。
- 完成翻译时务必：
  1. 更新此文档的状态与复选框。
  2. 同步 `docs/matrix/react-source-mapping.csv` 中的 `status` 与备注。
  3. 若引入自动化脚本（如 `check-parity`），在备注中记录覆盖方式。

---

## 模块索引

- [React Shared](#react-shared-模块) — Feature flags、常量、内部桥接。
- [React Reconciler](#react-reconciler-模块) — Fiber、Scheduler、WorkLoop。
- [React DOM](#react-dom-模块) — HostConfig、属性 diff、事件系统。
- [React Core](#react-core-模块) — Hooks、Context、入口 API。
- [Scheduler](#scheduler-模块) — 任务调度、优先级映射。
- [实验性模块](#实验性模块) — Flight、Server Components、Transition Tracing。

> 所有模块的 JS ↔️ C++ 结构化映射请参考 `docs/matrix/react-source-mapping.csv`。本文档用于补充逐函数 TODO；新增模块时，请在此加入章节并同步 CSV 状态。

---

## React Shared 模块

涵盖 `packages/shared` 下的常量与工具（如 `ReactFeatureFlags`、`ReactWorkTags`、`ReactSharedInternals`）。

- ✅ `ReactFeatureFlags`：自动生成并已对齐。后续需要在 Feature flag 更新时同步脚本产物。
- ✅ `ReactWorkTags` / `ReactFiberFlags`：常量与测试覆盖完备。
- 🔄 `ReactSharedInternals`：核心 API 已移植。待办：
  - [ ] DEV: 提供统一的日志/诊断接口，当访问 ReactSharedInternals 失败时输出调试信息。
  - [ ] Experimental: 追踪 `ReactSharedInternals` 新增键值（参考 upstream 变更）。

---

## React Reconciler 模块

### `ReactFiberRootScheduler`

- JS 源：`reactjs/packages/react-reconciler/src/ReactFiberRootScheduler.js`
- C++ 实现：`packages/React/src/ReactReconciler/ReactFiberRootScheduler.cpp`

### 函数映射总览

| JS 符号 | C++ 符号 | 状态 | 备注 |
| --- | --- | --- | --- |
| `ensureRootIsScheduled` | `ensureRootIsScheduled` | 🔄 | DEV `act` 支持刚补齐；仍需校验异常处理与多根场景（推荐优先验证）。 |
| `ensureScheduleIsScheduled` | `ensureScheduleIsScheduled` / `ensureScheduleProcessing` | 🔄 | `actQueue`/Safari fallback 已补，仍缺诊断日志。 |
| `flushSyncWorkOnAllRoots` | `flushSyncWorkOnAllRoots` | ✅ | 与 JS 功能对齐。 |
| `flushSyncWorkOnLegacyRootsOnly` | `flushSyncWorkOnLegacyRootsOnly` | ✅ | 行为一致。 |
| `flushSyncWorkAcrossRoots_impl` | `flushSyncWorkAcrossRoots` | 🔄 | 需补充 `performSyncWorkOnRoot` 垂直整合与 `flushPendingEffects` 调用。 |
| `processRootScheduleInImmediateTask` | `processRootScheduleInImmediateTask` | 🔄 | 包装已补齐，仍缺 `trackSchedulerEvent`（Profiler 集成）。 |
| `processRootScheduleInMicrotask` | `processRootScheduleInMicrotask` | ✅ | 重置 dedupe 标志，结合 Safari fallback 与 `act` 队列互斥。 |
| `startDefaultTransitionIndicatorIfNeeded` | `startDefaultTransitionIndicatorIfNeeded` | 🔄 | 与 JS 基本一致，仍缺错误上报差异化。 |
| `scheduleTaskForRootDuringMicrotask` | `scheduleTaskForRootDuringMicrotask` | 🔄 | 引入 fake act handle；需验证 `lanesToEventPriority`→Scheduler 映射与 continuation 复用策略。 |
| `performWorkOnRootViaSchedulerTask` | `performWorkOnRootViaSchedulerTask` | 🔄 | 并发入口已实现；待补 profiler `trackSchedulerEvent` 与 JS Continuation 语义验证。 |
| `performSyncWorkOnRoot` | `performSyncWorkOnRoot` | 🔄 | 新增同步渲染封装；仍需补 DEV `syncNestedUpdateFlag`。 |
| `scheduleCallback` | `scheduleCallback` | 🔄 | 支持 act 队列映射与 `didTimeout` 透传；仍缺 Scheduler continuation 回传。 |
| `cancelCallback` | `cancelCallback` | ✅ | 处理 act 队列移除与运行时任务取消。 |
| `scheduleImmediateRootScheduleTask` | `scheduleImmediateRootScheduleTask` | ✅ | 实现 `actQueue` 推入 + `supportsMicrotasks` 检测与 Safari fallback。 |
| `requestTransitionLane` | `requestTransitionLane` | ✅ | 行为一致。 |
| `didCurrentEventScheduleTransition` | `didCurrentEventScheduleTransition` | ✅ | 行为一致。 |
| `markIndicatorHandled` | `markIndicatorHandled` | ✅ | 行为一致。 |

### 详细检查清单

#### `ensureRootIsScheduled`

- [x] 将根节点加入调度队列（`addRootToSchedule`）。
- [x] 维护 `mightHavePendingSyncWork` 标志。
- [x] 触发微任务调度（`ensureScheduleProcessing`）。
- [x] DEV：在 Legacy Root + `isBatchingLegacy` 时写入 `didScheduleLegacyUpdate`。
- [ ] DEV：对 `ReactSharedInternals.didScheduleLegacyUpdate` 写入失败时记录诊断日志。

#### `ensureScheduleIsScheduled`

- [x] DEV：当 `actQueue` 存在时走 `didScheduleMicrotask_act` 分支。
- [x] PROD：以 `didScheduleMicrotask` 去重 microtask。
- [x] 调用 `scheduleImmediateRootScheduleTask`。
- [x] Safari fallback：`supportsMicrotasks` 为 false 时降级。

#### `flushSyncWorkAcrossRoots`

- [x] `isFlushingWork` 重入保护。
- [x] `mightHavePendingSyncWork` 快速退出。
- [x] 遍历 Root，调用 `getNextLanesToFlushSync` / `getNextLanes`。
- [ ] 同步执行应调用 `performSyncWorkOnRoot`，而非直接 `performWorkOnRoot`。
- [ ] 缺 `flushPendingEffects(true)` 前置处理。
- [ ] `scheduleCallback` 尚未处理 `didTimeout` 和回传 continuation。
- [ ] 处理 `onlyLegacy` 分支后，按 JS 行为维持 `root = root.next`。

#### `processRootSchedule`

- [x] 设置 `isProcessingRootSchedule`/`didScheduleRootProcessing`。
- [x] 计算 `syncTransitionLanes` 与 `mightHavePendingSyncWork`。
- [x] 完成根列表的增删与尾指针维护。
- [x] 若无待提交 effect，调用 `flushSyncWorkAcrossRoots`。
- [x] 通过 `processRootScheduleInMicrotask` 包装补充 Safari 微任务降级逻辑。
- [ ] 缺少 `trackSchedulerEvent`（Profiler 集成）。

#### `startDefaultTransitionIndicatorIfNeeded`

- [x] 遍历根节点并保留 isomorphic indicator。
- [x] 捕获 `onDefaultTransitionIndicator` 抛出的异常。
- [ ] 与 JS 端一致地使用 `noop` 常量（当前使用 lambda，与 JS 行为差异需验证）。

#### `scheduleTaskForRootDuringMicrotask`

- [x] 调用 `markStarvedLanesAsExpired`。
- [x] 选择 `nextLanes` 并维护 `callbackNode/priority`。
- [x] 处理同步 Lane 的快速路径。
- [x] 引入针对 `actQueue` 的 fake handle（高位标记 `TaskHandle`）以对齐测试场景。
- [x] 事件优先级转换：`toSchedulerPriority` 现基于 `lanesToEventPriority`。

#### `performWorkOnRootViaSchedulerTask`

- [x] 在执行 Scheduler 任务前刷新 pending passive effects。
- [x] 处理中断提交 (`hasPendingCommitEffects`) 并重置回调句柄。
- [x] 恢复 root 调度队列（`markStarvedLanesAsExpired` + `removeRootFromSchedule`）。
- [ ] 集成 `trackSchedulerEvent` 与 profiler 钩子。
- [ ] 覆盖 scheduler continuation（JS 通过返回值复用任务）。

#### `performSyncWorkOnRoot`

- [x] 在同步入口刷新 pending passive effects。
- [x] 垂直复用 `performWorkOnRoot(... forceSync=true)`。
- [ ] DEV：`enableProfilerNestedUpdatePhase` 时调用 `syncNestedUpdateFlag`。

#### `scheduleCallback` / `cancelCallback`

- [x] 为 act 队列生成稳定句柄（高位标记 + 递增 ID）。
- [x] 执行时移除 act 回调并释放句柄。
- [x] `cancelCallback` 支持 act 队列删除（数组压缩）。
- [ ] Scheduler continuation：当前 Runtime 分支仍总是返回 `nullptr`。

#### `scheduleImmediateRootScheduleTask`

- [x] 通过 Runtime 调度 `processRootSchedule`。
- [x] DEV：当 `actQueue` 存在时将回调推入 `act` 队列（`enqueueActMicrotask`）。
- [x] `supportsMicrotasks` 为 false 时降级到 Scheduler。
- [x] Safari iframe workaround：在渲染/提交上下文内回退到 Scheduler 宏任务。

#### 其余待翻译项

- [x] `processRootScheduleInImmediateTask` & `processRootScheduleInMicrotask` 拆分（仍缺 `trackSchedulerEvent` 钩子）。
- [x] `performWorkOnRootViaSchedulerTask` 并发调度入口。
- [x] `performSyncWorkOnRoot` 同步调度入口。
- [x] `scheduleCallback` / `cancelCallback` act 队列 handle 管理。
- [ ] Scheduler continuation 语义（`scheduleCallback` 返回值处理）。
- [x] 复刻 `fakeActCallbackNode` 常量与 act 流程（改用 `TaskHandle` sentinel）。

### 其余 Reconciler 文件

- 🔄 `ReactFiberWorkLoop`：核心渲染循环已迁移；`performUnitOfWork`、`beginWork`、`completeWork` 仍需逐行对齐。
  - [x] `updateContextProvider` / `updateContextConsumer` C++ 版本译制并接入 `beginWork`。
- 🔄 `ReactFiberThrow`：错误恢复路径已迁移；需与 `ReactFiberWorkLoop` 的异常流程联通。
- 🔄 `ReactFiberBeginWork` / `ReactFiberCompleteWork`：已接入 Fragment / Mode / Profiler 的入口逻辑；仍缺子 Fiber 对齐。
  - [x] `markRef` 旗标同步（Fragment refs）。
  - [x] `updateFragment` / `updateMode` / `updateProfiler` 初步移植。
  - [x] `reconcileChildren` 支持复刻 `cloneChildFibers`（避免直接复用 current 链接）。
  - [x] `attemptEarlyBailoutIfNoScheduledUpdate` 复用 `cloneChildFibers`，提前返回路径保持 child 链克隆。
  - [x] 复刻 resume-work 未实现的 invariant（触发 `Resuming work not yet implemented.`）。
  - [x] 独立 `ReactFiberChild.{h,cpp}` 文件抽取 clone/ reset 函数，方便后续补齐 child reconciler。
  - [x] `reconcileChildren` → `mountChildFibers` / `reconcileChildFibers` 初版接入（覆盖 null / 文本 / 单节点复用，并对线性数组实现 keyed diff）。
  - [x] `ReactFiberChild` 嵌套数组通过 Fragment Fiber 复用/创建逻辑已补齐。
  - [x] `ReactFiberChild` 普通 Iterator（同步 iterable）收集并复用数组 diff 路径。
  - [x] `ReactFiberChild` Portal 节点复用与创建逻辑（HostPortal key/container/implementation 匹配）。
  - [ ] `ReactFiberChild` Context / Profiler state node 等高级分支仍待补齐。
    - [x] Context consumer：移植 `readContextDuringReconciliation`（依赖 `ReactFiberNewContext` 翻译）。
    - [x] Lazy 节点：接入 `resolveLazy`、Suspense thenable 处理（依赖 `ReactFiberThenable` 完整实现）。
    - [x] Scope 组件：`updateScopeComponent` 推进 children diff + ref 标记。
    - [x] Portal 组件：`updatePortalComponent` 推栈容器并复刻挂载/更新路径。
    - [ ] Profiler state node：补充计时字段与 child placement 行为。
  - 🔄 `ReactFiberNewContext`：provider 栈与上下文传播逻辑正在移植。
    - [x] `pushProvider` / `popProvider` / `scheduleContextWorkOnParentPath`。
    - [x] `propagateContextChange`、`checkIfContextChanged` 以及依赖链匹配。
    - [x] Provider / Consumer begin 阶段上下文推栈与读取路径在 `ReactFiberWorkLoop` 中落地。
    - [ ] `lazilyPropagateParentContextChanges` 中的 HostTransition / 多 renderer DEV 分支。
  - [ ] Profiler `stateNode` 结构与计时字段对齐。
- ⛔ `ReactChildFiber`：Child reconciler 逻辑未移植。

> Reconciler 子模块 TODO 详见即将新增的小节，请根据实际进度补充细化的复选项。

#### `ReactFiberThenable`

- JS 源：`reactjs/packages/react-reconciler/src/ReactFiberThenable.js`
- C++ 状态：仅提供 `noopSuspenseyCommitThenable` 桥接（Wakeable 封装）。
- 待办：
  - [x] 翻译 `SuspenseException`/`SuspenseActionException` 与 `trackUsedThenable` 状态管理。
  - [x] 实现 `resolveLazy`，支持 `_init`/`_payload` 状态机并与 `Wakeable` 通道联动。
  - [x] 暴露 `getSuspendedThenable`、`checkIfUseWrappedInAsyncCatch` 等接口，供 beginWork / child reconciler 捕获。
  - [x] DEV：补充 `_debugInfo` / IO 采集逻辑。
  - [x] DEV：桥接 `callLazyInitInDEV`。

---

## React DOM 模块

- 🔄 `ReactDOMHostConfig`：宿主接口桩已建立；待补 `prepareUpdate`/`commitUpdate` 与事件优先级桥接。
- 🔄 `ReactDOMComponent` / `ReactDOMDiffProperties`：属性 diff 逻辑翻译中；需对照官方测试补充断言。
- ⛔ `DOMPluginEventSystem`、`LegacyEventPluginHub`：事件系统尚未翻译。
- ⛔ Hydration：`ReactFiberHydrationContext` & `ReactDOMHydration` 未开始。

建议在完成每个主文件时，将对应函数/表达式 checklist 添加到本文档。

---

## React Core 模块

- ⛔ Hooks：`ReactHooks`、`ReactFiberHooks` 模块未翻译。
- ⛔ Context：`ReactContext`、`ReactNewContext` 相关逻辑未翻译。
- ⛔ 入口 API：`ReactBaseClasses`、`React` 入口尚待处理。

当开始翻译 Hooks 或 Context 时，请以 `ReactFiberRootScheduler` 样例的方式建立子章节与 checklist。

---

## Scheduler 模块

- ⛔ `Scheduler`（JS fork）尚未对应到 `packages/React/src/scheduler/`。
- ⛔ `SchedulerPriorities` 未翻译。
- TODO：
  - [ ] 建立与 JS 端 `Scheduler` 行为一致的任务循环（MessageChannel/postTask 兼容）。
  - [ ] 添加 Scheduler 单测以覆盖优先级与时间片。

---

## 实验性模块

- ⛔ Flight：`ReactFlight*` 家族未翻译。
- ⛔ Server Components：`react-server` 与 `react-server-dom-*` 目录尚未翻译。
- ⛔ Transition Tracing：相关工具与 Integration 尚未移植。

如未来启动这些模块，请复制「函数映射总览」模板建立专章。

---

> 后续补充新模块时，请保持章节索引同步更新，确保 TODO 文档覆盖整个 reactjs 仓库。
