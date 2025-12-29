# ReactJS → JSI C++ 详细转译计划

> **版本**: 1.0.0  
> **日期**: 2025-12-27  
> **配套文档**: [技术方案](./JSI_React_Transpilation_TechSpec.md)

---

## 目录

- [1. 转译原则](#1-转译原则)
- [2. 迭代阶段总览](#2-迭代阶段总览)
- [3. 各阶段详细计划](#3-各阶段详细计划)
- [4. 模块优先级与依赖图](#4-模块优先级与依赖图)
- [5. 进度跟踪机制](#5-进度跟踪机制)
- [6. 单元测试规范](#6-单元测试规范)
- [7. 风险与应对](#7-风险与应对)
- [8. std::any DSL 类型整改](#8-stdany-dsl-类型整改)

---

## 1. 转译原则

### 1.1 核心原则

| 原则 | 说明 |
|------|------|
| **文件 1:1 对应** | 每个 JS 源文件对应唯一 C++ 文件 |
| **模块 1:1 对应** | 保持模块边界、导出接口完全一致 |
| **函数 1:1 对应** | 每个函数在对照表中有精确映射 |
| **流程完全对齐** | 执行逻辑、控制流与 JS 端一致 |

### 1.2 转译工作流

```
┌─────────────────────────────────────────────────────────────────┐
│                      每个 JS 文件的转译流程                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  1. 解析阶段                                                      │
│     ├─ 运行 generate-react-mapping 脚本                          │
│     ├─ 生成函数级对照表 (JSONL)                                   │
│     └─ 更新 react-function-map.jsonl                             │
│                                                                  │
│  2. 骨架生成                                                      │
│     ├─ 根据对照表生成 C++ 头文件 (.h)                             │
│     ├─ 生成源文件骨架 (.cpp)                                      │
│     └─ 标记 TODO 注释与 JS 源码行号                               │
│                                                                  │
│  3. 逐函数翻译                                                    │
│     ├─ 按依赖顺序翻译每个函数                                      │
│     ├─ 保持变量命名、控制流对齐                                    │
│     └─ 更新对照表状态为 "in-progress"                             │
│                                                                  │
│  4. 单元测试                                                      │
│     ├─ 为每个导出函数编写 gtest                                   │
│     ├─ 参照 JS 端测试用例                                         │
│     └─ 确保行为一致                                               │
│                                                                  │
│  5. 验收与合并                                                    │
│     ├─ 更新对照表状态为 "completed"                               │
│     ├─ 更新进度日志                                               │
│     └─ CI 验证通过后合并                                          │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. 迭代阶段总览

| 阶段 | 名称 | 核心目标 | 预计周期 | 依赖 |
|------|------|----------|----------|------|
| **Phase 0** | 基础设施 | 工具链、对照表生成、CI 配置 | 1 周 | - |
| **Phase 1** | Shared 模块 | Feature Flags、常量、工具函数 | 2 周 | Phase 0 |
| **Phase 2** | Scheduler 模块 | 任务队列、优先级、调度循环 | 2 周 | Phase 1 |
| **Phase 3** | Fiber 数据结构 | FiberNode、FiberRoot、Lane | 3 周 | Phase 1 |
| **Phase 4** | Reconciler 核心 | BeginWork、CompleteWork、Commit | 4 周 | Phase 2, 3 |
| **Phase 5** | React Core | createElement、Hooks、Context | 3 周 | Phase 4 |
| **Phase 6** | HostConfig 集成 | 宿主配置、DOM 绑定（可选） | 2 周 | Phase 4 |
| **Phase 7** | 端到端验证 | 集成测试、性能基准 | 2 周 | Phase 5, 6 |

**总预计周期**: 19 周（约 5 个月）

---

## 3. 各阶段详细计划

### Phase 0: 基础设施 (1 周)

#### 目标
- 建立工具链和自动化流程
- 生成完整的模块-函数对照表
- 配置 CI/CD 流水线

#### 任务列表

| 任务 ID | 任务描述 | 预计工时 | 状态 |
|---------|----------|----------|------|
| P0-001 | 完善 `generate-react-mapping.js` 脚本 | 4h | ✅ 完成 |
| P0-002 | 生成完整 `react-function-map.jsonl` | 2h | ✅ 完成 |
| P0-003 | 生成 `react-exports-map.json` | 2h | ✅ 完成 |
| P0-004 | 创建 C++ 骨架生成脚本 | 8h | ⏳ 进行中 |
| P0-005 | 配置 CMake 构建系统 | 4h | ⏳ 进行中 |
| P0-006 | 配置 gtest 测试框架 | 4h | 📋 待开始 |
| P0-007 | 设置 CI 流水线 (GitHub Actions) | 4h | 📋 待开始 |
| P0-008 | 创建进度跟踪模板 | 2h | 📋 待开始 |

#### 交付物
- [x] `scripts/generate-react-mapping.js`
- [x] `docs/matrix/react-function-map.jsonl`
- [x] `docs/matrix/react-exports-map.json`
- [ ] `scripts/generate-cpp-skeleton.js`
- [ ] `packages/React/CMakeLists.txt`
- [ ] `.github/workflows/ci.yml`

---

### Phase 1: Shared 模块 (2 周)

#### 目标
- 翻译所有共享常量、工具函数
- 建立 Feature Flag 系统
- 完成 ReactWorkTags、ReactFiberFlags 等核心枚举

#### 模块清单

| 源文件 | 目标文件 | 函数数 | 优先级 |
|--------|----------|--------|--------|
| `shared/ReactFeatureFlags.js` | `shared/ReactFeatureFlags.cpp` | 50+ flags | P0 |
| `shared/ReactWorkTags.js` | `shared/ReactWorkTags.cpp` | 1 enum | P0 |
| `shared/ReactSymbols.js` | `shared/ReactSymbols.cpp` | 15 symbols | P0 |
| `shared/ReactSharedInternals.js` | `shared/ReactSharedInternals.cpp` | 5 exports | P0 |
| `shared/ReactTypes.js` | `shared/ReactTypes.h` | types only | P0 |
| `shared/ReactElementType.js` | `shared/ReactElementType.h` | types only | P0 |
| `shared/shallowEqual.js` | `shared/shallowEqual.cpp` | 1 function | P1 |
| `shared/objectIs.js` | `shared/objectIs.cpp` | 1 function | P1 |
| `shared/hasOwnProperty.js` | `shared/hasOwnProperty.cpp` | 1 function | P1 |
| `shared/isArray.js` | `shared/isArray.cpp` | 1 function | P1 |
| `shared/assign.js` | `shared/assign.cpp` | 1 function | P1 |
| `shared/CheckStringCoercion.js` | `shared/CheckStringCoercion.cpp` | 4 functions | P2 |
| `shared/getComponentNameFromType.js` | `shared/getComponentNameFromType.cpp` | 1 function | P2 |

#### 任务列表

| 任务 ID | 任务描述 | 预计工时 | 依赖 | 状态 |
|---------|----------|----------|------|------|
| P1-001 | 翻译 ReactFeatureFlags | 8h | P0 | ✅ 完成 |
| P1-002 | 翻译 ReactWorkTags | 2h | P0 | ✅ 完成 |
| P1-003 | 翻译 ReactSymbols | 4h | P0 | ✅ 完成 |
| P1-004 | 翻译 ReactSharedInternals | 4h | P1-001 | ⏳ 进行中 |
| P1-005 | 翻译 shallowEqual | 2h | P0 | 📋 待开始 |
| P1-006 | 翻译 objectIs | 1h | P0 | 📋 待开始 |
| P1-007 | 翻译 hasOwnProperty | 1h | P0 | 📋 待开始 |
| P1-008 | 翻译 isArray | 1h | P0 | 📋 待开始 |
| P1-009 | 翻译 assign | 2h | P0 | 📋 待开始 |
| P1-010 | 翻译 CheckStringCoercion | 4h | P1-004 | 📋 待开始 |
| P1-011 | 翻译 getComponentNameFromType | 4h | P1-003 | 📋 待开始 |
| P1-012 | 编写 Shared 模块单元测试 | 8h | P1-001~011 | 📋 待开始 |
| P1-013 | 验证常量值与 JS 快照一致 | 4h | P1-012 | 📋 待开始 |

#### 验收标准
- [ ] 所有 Feature Flag 常量与 JS 端值完全一致
- [ ] WorkTags 枚举值与 JS 端一致
- [ ] 单元测试覆盖率 ≥ 95%
- [ ] CI 通过

---

### Phase 2: Scheduler 模块 (2 周)

#### 目标
- 翻译 Scheduler 核心逻辑
- 实现 SchedulerHost 抽象层
- 完成优先级队列和任务调度

#### 模块清单

| 源文件 | 目标文件 | 函数数 | 优先级 |
|--------|----------|--------|--------|
| `scheduler/src/SchedulerPriorities.js` | `scheduler/SchedulerPriorities.cpp` | 6 constants | P0 |
| `scheduler/src/SchedulerMinHeap.js` | `scheduler/SchedulerMinHeap.cpp` | 5 functions | P0 |
| `scheduler/src/SchedulerFeatureFlags.js` | `scheduler/SchedulerFeatureFlags.cpp` | 3 flags | P0 |
| `scheduler/src/forks/Scheduler.js` | `scheduler/Scheduler.cpp` | 20+ functions | P0 |
| `scheduler/src/SchedulerProfiling.js` | `scheduler/SchedulerProfiling.cpp` | 10 functions | P2 |

#### 核心函数对照

| JS 函数 | C++ 函数 | 说明 |
|---------|----------|------|
| `unstable_scheduleCallback` | `scheduleCallback` | 调度任务 |
| `unstable_cancelCallback` | `cancelCallback` | 取消任务 |
| `unstable_getCurrentPriorityLevel` | `getCurrentPriorityLevel` | 获取当前优先级 |
| `unstable_shouldYield` | `shouldYield` | 判断是否应该让出 |
| `unstable_requestPaint` | `requestPaint` | 请求绘制 |
| `unstable_runWithPriority` | `runWithPriority` | 以指定优先级运行 |
| `workLoop` | `workLoop` | 主工作循环 |
| `flushWork` | `flushWork` | 刷新任务队列 |

#### SchedulerHost 抽象实现

```cpp
// 由 ReactHostRuntime 提供的 SchedulerHost 实现
class DefaultSchedulerHost : public SchedulerHost {
public:
    double getCurrentTime() override {
        // 使用 std::chrono 或平台特定 API
    }
    
    void requestHostCallback(std::function<bool(double)> callback) override {
        // MessageChannel 模拟或 setImmediate 等价
    }
    
    bool shouldYieldToHost() override {
        // 基于时间片判断
    }
    // ...
};
```

#### 任务列表

| 任务 ID | 任务描述 | 预计工时 | 依赖 | 状态 |
|---------|----------|----------|------|------|
| P2-001 | 翻译 SchedulerPriorities | 2h | P1 | 📋 待开始 |
| P2-002 | 翻译 SchedulerMinHeap | 4h | P1 | 📋 待开始 |
| P2-003 | 翻译 SchedulerFeatureFlags | 1h | P1-001 | 📋 待开始 |
| P2-004 | 设计 SchedulerHost 接口 | 4h | P1 | 📋 待开始 |
| P2-005 | 翻译 Scheduler 核心 (Part 1) | 8h | P2-001~004 | 📋 待开始 |
| P2-006 | 翻译 Scheduler 核心 (Part 2) | 8h | P2-005 | 📋 待开始 |
| P2-007 | 实现 DefaultSchedulerHost | 8h | P2-004 | 📋 待开始 |
| P2-008 | 翻译 SchedulerProfiling (可选) | 8h | P2-005 | 📋 待开始 |
| P2-009 | 编写 Scheduler 单元测试 | 8h | P2-006 | 📋 待开始 |
| P2-010 | 集成测试与验证 | 4h | P2-009 | 📋 待开始 |

#### 验收标准
- [ ] scheduleCallback/cancelCallback 功能正确
- [ ] 优先级队列排序正确
- [ ] shouldYield 时间片逻辑正确
- [ ] 单元测试覆盖率 ≥ 90%

---

### Phase 3: Fiber 数据结构 (3 周)

#### 目标
- 翻译 Fiber 节点数据结构
- 实现 Lane 优先级模型
- 完成 FiberRoot 和更新队列

#### 模块清单

| 源文件 | 目标文件 | 函数数 | 优先级 |
|--------|----------|--------|--------|
| `react-reconciler/src/ReactFiber.js` | `reconciler/ReactFiber.cpp` | 15 functions | P0 |
| `react-reconciler/src/ReactFiberLane.js` | `reconciler/ReactFiberLane.cpp` | 40+ functions | P0 |
| `react-reconciler/src/ReactFiberRoot.js` | `reconciler/ReactFiberRoot.cpp` | 5 functions | P0 |
| `react-reconciler/src/ReactFiberFlags.js` | `reconciler/ReactFiberFlags.cpp` | constants | P0 |
| `react-reconciler/src/ReactWorkTags.js` | `reconciler/ReactWorkTags.cpp` | enum | P0 |
| `react-reconciler/src/ReactFiberClassUpdateQueue.js` | `reconciler/ReactFiberUpdateQueue.cpp` | 10 functions | P1 |
| `react-reconciler/src/ReactFiberConcurrentUpdates.js` | `reconciler/ReactFiberConcurrentUpdates.cpp` | 8 functions | P1 |
| `react-reconciler/src/ReactFiberStack.js` | `reconciler/ReactFiberStack.cpp` | 5 functions | P1 |

#### Lane 模型核心函数

| JS 函数 | C++ 函数 | 说明 |
|---------|----------|------|
| `mergeLanes` | `mergeLanes` | 合并 Lane |
| `intersectLanes` | `intersectLanes` | Lane 交集 |
| `isSubsetOfLanes` | `isSubsetOfLanes` | 子集判断 |
| `includesSomeLane` | `includesSomeLane` | 包含判断 |
| `getHighestPriorityLane` | `getHighestPriorityLane` | 最高优先级 |
| `getNextLanes` | `getNextLanes` | 获取下一批 Lane |
| `markRootUpdated` | `markRootUpdated` | 标记根更新 |
| `markRootFinished` | `markRootFinished` | 标记根完成 |

#### 任务列表

| 任务 ID | 任务描述 | 预计工时 | 依赖 | 状态 |
|---------|----------|----------|------|------|
| P3-001 | 翻译 ReactFiberFlags | 2h | P1 | ✅ 完成 |
| P3-002 | 翻译 ReactWorkTags | 2h | P1 | ✅ 完成 |
| P3-003 | 翻译 ReactFiberLane 常量 | 4h | P3-001 | ✅ 完成 |
| P3-004 | 翻译 ReactFiberLane 工具函数 | 12h | P3-003 | ⏳ 进行中 |
| P3-005 | 定义 FiberNode 结构体 | 8h | P3-002 | ✅ 完成 |
| P3-006 | 翻译 ReactFiber 创建函数 | 8h | P3-005 | ⏳ 进行中 |
| P3-007 | 定义 FiberRoot 结构体 | 4h | P3-003 | ✅ 完成 |
| P3-008 | 翻译 ReactFiberRoot 函数 | 8h | P3-007 | ⏳ 进行中 |
| P3-009 | 翻译 ReactFiberUpdateQueue | 12h | P3-006 | ⏳ 进行中 |
| P3-010 | 翻译 ReactFiberConcurrentUpdates | 8h | P3-009 | ⏳ 进行中 |
| P3-011 | 翻译 ReactFiberStack | 4h | P3-005 | 📋 待开始 |
| P3-012 | 编写 Fiber 数据结构单元测试 | 12h | P3-001~011 | 📋 待开始 |

#### 验收标准
- [ ] FiberNode 所有字段与 JS 端一致
- [ ] Lane 常量值与 JS 端一致
- [ ] Lane 操作函数行为与 JS 端一致
- [ ] 单元测试覆盖率 ≥ 85%

---

### Phase 4: Reconciler 核心 (4 周)

#### 目标
- 翻译 WorkLoop 核心循环
- 实现 BeginWork、CompleteWork、CommitWork
- 完成同步渲染路径

#### 模块清单

| 源文件 | 目标文件 | 函数数 | 优先级 |
|--------|----------|--------|--------|
| `ReactFiberWorkLoop.js` | `ReactFiberWorkLoop.cpp` | 30+ functions | P0 |
| `ReactFiberBeginWork.js` | `ReactFiberBeginWork.cpp` | 40+ functions | P0 |
| `ReactFiberCompleteWork.js` | `ReactFiberCompleteWork.cpp` | 20+ functions | P0 |
| `ReactFiberCommitWork.js` | `ReactFiberCommitWork.cpp` | 30+ functions | P0 |
| `ReactFiberReconciler.js` | `ReactFiberReconciler.cpp` | 15 functions | P0 |
| `ReactChildFiber.js` | `ReactChildFiber.cpp` | 15 functions | P1 |
| `ReactFiberHooks.js` | `ReactFiberHooks.cpp` | 40+ functions | P1 |
| `ReactFiberThrow.js` | `ReactFiberThrow.cpp` | 10 functions | P1 |
| `ReactFiberUnwindWork.js` | `ReactFiberUnwindWork.cpp` | 5 functions | P1 |

#### WorkLoop 核心函数

| JS 函数 | C++ 函数 | 说明 |
|---------|----------|------|
| `performSyncWorkOnRoot` | `performSyncWorkOnRoot` | 同步渲染入口 |
| `performConcurrentWorkOnRoot` | `performConcurrentWorkOnRoot` | 并发渲染入口 |
| `renderRootSync` | `renderRootSync` | 同步渲染根 |
| `renderRootConcurrent` | `renderRootConcurrent` | 并发渲染根 |
| `workLoopSync` | `workLoopSync` | 同步工作循环 |
| `workLoopConcurrent` | `workLoopConcurrent` | 并发工作循环 |
| `performUnitOfWork` | `performUnitOfWork` | 执行工作单元 |
| `commitRoot` | `commitRoot` | 提交渲染结果 |

#### 任务列表

| 任务 ID | 任务描述 | 预计工时 | 依赖 | 状态 |
|---------|----------|----------|------|------|
| P4-001 | 翻译 ReactFiberReconciler 入口 | 8h | P3 | 📋 待开始 |
| P4-002 | 翻译 workLoopSync | 8h | P4-001 | 📋 待开始 |
| P4-003 | 翻译 performUnitOfWork | 4h | P4-002 | 📋 待开始 |
| P4-004 | 翻译 BeginWork 主函数 | 8h | P4-003 | 📋 待开始 |
| P4-005 | 翻译 BeginWork 各类型处理 | 24h | P4-004 | 📋 待开始 |
| P4-006 | 翻译 CompleteWork 主函数 | 8h | P4-005 | 📋 待开始 |
| P4-007 | 翻译 CompleteWork 各类型处理 | 16h | P4-006 | 📋 待开始 |
| P4-008 | 翻译 CommitRoot | 8h | P4-007 | 📋 待开始 |
| P4-009 | 翻译 CommitWork 效果处理 | 16h | P4-008 | 📋 待开始 |
| P4-010 | 翻译 ReactChildFiber | 12h | P4-005 | 📋 待开始 |
| P4-011 | 翻译 ReactFiberHooks (Part 1) | 16h | P4-005 | 📋 待开始 |
| P4-012 | 翻译 ReactFiberHooks (Part 2) | 16h | P4-011 | 📋 待开始 |
| P4-013 | 翻译 ReactFiberThrow | 8h | P4-009 | 📋 待开始 |
| P4-014 | 翻译 ReactFiberUnwindWork | 4h | P4-013 | 📋 待开始 |
| P4-015 | 编写 Reconciler 单元测试 | 16h | P4-001~014 | 📋 待开始 |
| P4-016 | 同步渲染路径集成测试 | 8h | P4-015 | 📋 待开始 |

#### 验收标准
- [ ] 同步渲染路径完整可运行
- [ ] BeginWork 支持 HostComponent, HostText, FunctionComponent
- [ ] CommitWork 正确调用 HostConfig
- [ ] 单元测试覆盖率 ≥ 85%

---

### Phase 5: React Core (3 周)

#### 目标
- 翻译 React 核心 API
- 实现 Hooks 系统
- 完成 Context API

#### 模块清单

| 源文件 | 目标文件 | 函数数 | 优先级 |
|--------|----------|--------|--------|
| `react/src/ReactElement.js` | `react/ReactElement.cpp` | 10 functions | P0 |
| `react/src/ReactHooks.js` | `react/ReactHooks.cpp` | 15 functions | P0 |
| `react/src/ReactContext.js` | `react/ReactContext.cpp` | 3 functions | P0 |
| `react/src/ReactMemo.js` | `react/ReactMemo.cpp` | 1 function | P1 |
| `react/src/ReactLazy.js` | `react/ReactLazy.cpp` | 1 function | P1 |
| `react/src/ReactForwardRef.js` | `react/ReactForwardRef.cpp` | 1 function | P1 |
| `react/src/ReactChildren.js` | `react/ReactChildren.cpp` | 5 functions | P2 |

#### 任务列表

| 任务 ID | 任务描述 | 预计工时 | 依赖 | 状态 |
|---------|----------|----------|------|------|
| P5-001 | 翻译 ReactElement | 8h | P4 | 📋 待开始 |
| P5-002 | 翻译 ReactHooks 入口 | 8h | P4-012 | 📋 待开始 |
| P5-003 | 实现 useState | 8h | P5-002 | 📋 待开始 |
| P5-004 | 实现 useEffect | 8h | P5-002 | 📋 待开始 |
| P5-005 | 实现 useContext | 4h | P5-002 | 📋 待开始 |
| P5-006 | 实现 useRef | 4h | P5-002 | 📋 待开始 |
| P5-007 | 实现 useMemo/useCallback | 4h | P5-002 | 📋 待开始 |
| P5-008 | 实现 useReducer | 4h | P5-003 | 📋 待开始 |
| P5-009 | 翻译 ReactContext | 8h | P5-005 | 📋 待开始 |
| P5-010 | 翻译 ReactMemo | 4h | P5-001 | 📋 待开始 |
| P5-011 | 翻译 ReactLazy | 4h | P5-001 | 📋 待开始 |
| P5-012 | 翻译 ReactForwardRef | 4h | P5-001 | 📋 待开始 |
| P5-013 | 翻译 ReactChildren | 8h | P5-001 | 📋 待开始 |
| P5-014 | 编写 React Core 单元测试 | 12h | P5-001~013 | 📋 待开始 |

#### 验收标准
- [ ] createElement 功能正确
- [ ] 所有基础 Hooks 功能正确
- [ ] Context 读写正确
- [ ] 单元测试覆盖率 ≥ 90%

---

### Phase 6: HostConfig 集成 (2 周)

#### 目标
- 完成 HostConfig 抽象层
- 实现 Mock HostConfig 用于测试
- 可选：实现 DOM HostConfig

#### 任务列表

| 任务 ID | 任务描述 | 预计工时 | 依赖 | 状态 |
|---------|----------|----------|------|------|
| P6-001 | 完善 HostConfig 接口 | 8h | P4 | 📋 待开始 |
| P6-002 | 实现 NoopHostConfig (测试用) | 8h | P6-001 | 📋 待开始 |
| P6-003 | 实现 MockHostConfig | 8h | P6-001 | 📋 待开始 |
| P6-004 | 翻译 ReactDOMHostConfig (可选) | 16h | P6-001 | 📋 待开始 |
| P6-005 | 编写 HostConfig 集成测试 | 8h | P6-002 | 📋 待开始 |

---

### Phase 7: 端到端验证 (2 周)

#### 目标
- 完成端到端集成测试
- 性能基准测试
- 文档完善

#### 任务列表

| 任务 ID | 任务描述 | 预计工时 | 依赖 | 状态 |
|---------|----------|----------|------|------|
| P7-001 | 编写端到端测试套件 | 16h | P5, P6 | 📋 待开始 |
| P7-002 | 对照 React 官方测试 | 16h | P7-001 | 📋 待开始 |
| P7-003 | 性能基准测试 | 8h | P7-001 | 📋 待开始 |
| P7-004 | 更新文档 | 8h | P7-001 | 📋 待开始 |
| P7-005 | 最终验收 | 4h | P7-001~004 | 📋 待开始 |

---

## 4. 模块优先级与依赖图

```
                            ┌─────────────┐
                            │   Phase 0   │
                            │  基础设施   │
                            └──────┬──────┘
                                   │
                            ┌──────▼──────┐
                            │   Phase 1   │
                            │   Shared    │
                            └──────┬──────┘
                                   │
               ┌───────────────────┼───────────────────┐
               │                   │                   │
        ┌──────▼──────┐     ┌──────▼──────┐           │
        │   Phase 2   │     │   Phase 3   │           │
        │  Scheduler  │     │    Fiber    │           │
        └──────┬──────┘     └──────┬──────┘           │
               │                   │                   │
               └───────────┬───────┘                   │
                           │                           │
                    ┌──────▼──────┐                    │
                    │   Phase 4   │                    │
                    │  Reconciler │                    │
                    └──────┬──────┘                    │
                           │                           │
               ┌───────────┼───────────┐               │
               │           │           │               │
        ┌──────▼──────┐    │    ┌──────▼──────┐       │
        │   Phase 5   │    │    │   Phase 6   │       │
        │ React Core  │    │    │ HostConfig  │       │
        └──────┬──────┘    │    └──────┬──────┘       │
               │           │           │               │
               └───────────┴───────────┘               │
                           │                           │
                    ┌──────▼──────┐                    │
                    │   Phase 7   │                    │
                    │   验证     │                    │
                    └─────────────┘                    │
```

---

## 5. 进度跟踪机制

### 5.1 每日进度更新

每个工作日结束时更新 `docs/matrix/progress-log.md`：

```markdown
## 2025-12-27

### 完成项
- [x] P1-001: 翻译 ReactFeatureFlags (8h → 实际 10h)
- [x] P1-002: 翻译 ReactWorkTags (2h → 实际 2h)

### 进行中
- [ ] P1-003: 翻译 ReactSymbols (预计 4h, 已完成 50%)

### 遇到问题
- ReactSharedInternals 依赖需要先完成 dispatcher 模式设计

### 明日计划
- 完成 P1-003
- 开始 P1-004

### 测试状态
- 通过: 15/15
- 失败: 0
- 覆盖率: 96.2%
```

### 5.2 周进度汇报

每周五生成周报：

```markdown
# 周报 2025-W52

## 本周完成
- Phase 1 进度: 60% → 85%
- 完成模块: ReactFeatureFlags, ReactWorkTags, ReactSymbols
- 新增测试: 45 个
- 代码行数: +2,340

## 风险项
- ReactSharedInternals 复杂度超预期，需额外 1 天

## 下周计划
- 完成 Phase 1 剩余任务
- 开始 Phase 2

## 代码质量
- 测试覆盖率: 94.5%
- 静态分析警告: 0
```

### 5.3 对照表状态同步

使用脚本自动更新对照表状态：

```bash
# 更新函数状态
npm run update-mapping-status -- \
  --file "reactjs/packages/shared/ReactFeatureFlags.js" \
  --status "completed"

# 生成进度报告
npm run generate-progress-report
```

---

## 6. 单元测试规范

### 6.1 测试文件命名

```
源文件: ReactFiberLane.cpp
测试文件: __tests__/ReactFiberLaneTests.cpp
```

### 6.2 测试分类

| 类型 | 前缀 | 说明 |
|------|------|------|
| 单元测试 | `TEST_` | 单个函数测试 |
| 集成测试 | `INTEGRATION_TEST_` | 多模块协作测试 |
| 端到端测试 | `E2E_TEST_` | 完整流程测试 |
| 回归测试 | `REGRESSION_TEST_` | 防止回归的测试 |

### 6.3 测试模板

```cpp
#include <gtest/gtest.h>
#include "reconciler/ReactFiberLane.h"

namespace react::reconciler::tests {

// 测试常量值
TEST(ReactFiberLaneTest, LaneConstants_MatchJSValues) {
    // 对照 JS: const SyncLane = 0b0000000000000000000000000000010
    EXPECT_EQ(SyncLane, 0b0000000000000000000000000000010);
}

// 测试函数行为
TEST(ReactFiberLaneTest, MergeLanes_CombinesTwoLanes) {
    Lane a = SyncLane;
    Lane b = DefaultLane;
    Lane result = mergeLanes(a, b);
    
    EXPECT_TRUE(includesSomeLane(result, SyncLane));
    EXPECT_TRUE(includesSomeLane(result, DefaultLane));
}

// 参数化测试
class LanePriorityTest : public ::testing::TestWithParam<std::pair<Lane, int>> {};

TEST_P(LanePriorityTest, GetLanePriority_ReturnsCorrectPriority) {
    auto [lane, expectedPriority] = GetParam();
    EXPECT_EQ(getLanePriority(lane), expectedPriority);
}

INSTANTIATE_TEST_SUITE_P(
    LanePriorities,
    LanePriorityTest,
    ::testing::Values(
        std::make_pair(SyncLane, 15),
        std::make_pair(DefaultLane, 10),
        std::make_pair(IdleLane, 1)
    )
);

} // namespace react::reconciler::tests
```

### 6.4 测试要求

| 模块 | 最低覆盖率 | 必测函数 |
|------|-----------|----------|
| shared | 95% | 所有导出函数 |
| scheduler | 90% | scheduleCallback, cancelCallback, workLoop |
| reconciler | 85% | performSyncWorkOnRoot, beginWork, completeWork, commitRoot |
| react | 90% | createElement, useState, useEffect, useContext |

---

## 7. 风险与应对

---

## 8. std::any DSL 类型整改

为保证“开发者 DSL/JS 世界的数据”统一用 `facebook::jsi::Value` 表达，我们对仓库内 `std::any` 的使用做了专项审计，并将整改拆分为可落地的阶段任务。

- TODO 列表与阶段划分：
    - [docs/reactcpp/STD_ANY_DSL_Audit_TODO.md](docs/reactcpp/STD_ANY_DSL_Audit_TODO.md)

关键约束：本仓库的 `jsi::Value` 无拷贝构造（只能移动；需要 `Value(Runtime&, const Value&)` 显式克隆），因此“可复制结构体 + std::any 存储”场景需要先定迁移方案，再推进替换。

### 7.1 技术风险

| 风险 | 影响 | 概率 | 应对措施 |
|------|------|------|----------|
| JSI 类型系统差异 | 高 | 中 | 预研 JSI 边界案例，编写类型转换工具 |
| 闭包语义差异 | 中 | 高 | 使用 C++ lambda 捕获，必要时重构为类 |
| 并发模型差异 | 高 | 中 | SchedulerHost 抽象隔离平台差异 |
| ReactJS 版本更新 | 中 | 低 | 锁定基准版本，定期同步 |

### 7.2 进度风险

| 风险 | 影响 | 概率 | 应对措施 |
|------|------|------|----------|
| Reconciler 复杂度超预期 | 高 | 中 | 预留 20% 缓冲时间 |
| 测试覆盖不足 | 中 | 中 | 强制 PR 覆盖率门槛 |
| 文档滞后 | 低 | 高 | 代码即文档，强制注释 |

### 7.3 应急预案

1. **进度延迟 > 1 周**：重新评估范围，考虑延期或缩减
2. **关键 bug 无法解决**：升级到架构讨论，考虑绕行方案
3. **ReactJS 重大更新**：冻结当前版本，完成后再同步

---

## 附录 A: 文件统计

基于 `react-function-map.jsonl` 的统计：

| 模块 | 文件数 | 导出函数数 | 内部函数数 | 总函数数 |
|------|--------|-----------|-----------|---------|
| shared | 35 | 89 | 45 | 134 |
| scheduler | 6 | 25 | 30 | 55 |
| react | 25 | 45 | 60 | 105 |
| react-reconciler | 78 | 120 | 350 | 470 |
| react-dom | 45 | 60 | 180 | 240 |
| **总计** | **189** | **339** | **665** | **1004** |

---

## 附录 B: 检查清单

### 每个函数转译完成检查

- [ ] 函数签名与 JS 端一致
- [ ] 参数类型正确映射
- [ ] 返回值类型正确
- [ ] 控制流逻辑对齐
- [ ] 错误处理一致
- [ ] 添加源码行号注释
- [ ] 编写单元测试
- [ ] 更新对照表状态

### 每个文件转译完成检查

- [ ] 所有导出函数已翻译
- [ ] 所有内部函数已翻译
- [ ] 头文件正确声明
- [ ] 命名空间正确
- [ ] 包含必要的头文件
- [ ] 无编译警告
- [ ] 单元测试通过
- [ ] 覆盖率达标
- [ ] 更新进度日志

---

*最后更新: 2025-12-27*
