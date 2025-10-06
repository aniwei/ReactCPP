# ReactCPP Roadmap（2025 Q4 版）

> 目标：以 `react-main` 为唯一事实来源（SSOT），逐文件、逐函数完成 C++/JSI/Wasm 复刻，实现与官方 React 的行为、命名、结构 100% 对齐。

---

## 1. 愿景与里程碑

| 阶段 | 范围 | 关键交付 | 状态（2025-10-03） | 目标完成时间 |
| --- | --- | --- | --- | --- |
| Phase 0 | 目录映射、AST 模板、parity 工具 | `translate-react.js`、`check-parity.js`、源映射 CSV | 🟡 进行中（parity 报告需修复路径） | 2025-10-20 |
| Phase 1 | shared 常量/flag scaffold | `ReactFeatureFlags`、`ReactWorkTags`、`ReactFiberFlags` | 🟢 基本完成，测试覆盖已落地 | 2025-10-31 |
| Phase 2 | ReactDOM Host Parity | `ReactDOMHostConfig`、`ReactDOMComponent`、属性 diff | 🔴 未启动（仅有占位引用） | 2025-11-15 |
| Phase 3 | Fiber 数据结构 | Fiber/Lane/UpdateQueue/ConcurrentUpdates | 🟡 进行中（结构已翻译，核心算法缺口） | 2025-11-29 |
| Phase 4 | WorkLoop & Commit | `beginWork`/`completeWork`/`commit*` | 🔴 初始骨架，有大量 TODO | 2025-12-20 |
| Phase 5 | Scheduler 集成 | `requestHostCallback`、优先级策略 | ⚪ 未开始 | 2026-01-10 |
| Phase 6 | Hydration & DOM 事件 | Hydration 上下文、插件系统 | ⚪ 未开始 | 2026-02-07 |
| Phase 7 | Hooks & Context | Hook dispatcher、Context 注册 | ⚪ 未开始 | 2026-03-14 |
| Phase 8 | 官方测试对齐 | Jest 子集、Wasm 桥、CI | ⚪ 未开始 | 2026-Q2 |
| Phase 9 | Wasm 产线 & 调优 | 浏览器 demo、Benchmark | ⚪ 未开始 | 2026-Q2 |

---

## 2. 当前完成度快照

- **ReactReconciler 翻译覆盖**：41 / 80 个上游 `.js` 文件（≈51%），核心 WorkLoop / Commit / Hooks 仍缺失。
- **shared 层**：`ReactFeatureFlags`、`ReactWorkTags`、`ReactFiberFlags`、`ReactSymbols`、`ReactSharedInternals` 等已对齐，配有 gtest 快照。
- **Fiber 数据结构**：`FiberNode`、`FiberRootNode`、Lane 模型、UpdateQueue 基本落地，但 `ReactFiberClassComponent.js` 等逻辑尚未迁移。
- **Runtime/Host**：`ReactRuntime.cpp` 暂用手写 DOM diff，`ReactDOM/client` 目录没有对应实现文件，HostConfig 尚未启动。
- **调度器**：仅存在空壳 `Scheduler.h`，未实现任务驱动与优先级。
- **工具链**：`translate-react.js` 可生成骨架；`check-parity.js` 因路径指向 `packages/ReactCpp/src` 报错，需要改为 `packages/React/src` 后才能产出报告。

---

## 3. 关键差距与 Blocker

1. **WorkLoop 核心函数缺失**：`ReactFiberWorkLoop.cpp` 中 `beginWork`、`completeWork`、`unwindWork` 仍为 TODO，无法驱动真实 Fiber 流程。
2. **HostConfig 未翻译**：CMake 已引用 `ReactDOMComponent.cpp` 等文件，但目录为空；需尽快启动 DOM 宿主移植以取代临时 reconciler。
3. **Scheduler 未集成**：缺少 `requestHostCallback` 等函数，不能调度不同优先级的更新。
4. **Hooks/Context/Hydration**：上游大部分高级特性尚未开始翻译。
5. **Parity 指标缺失**：当前无法输出翻译覆盖率/缺口报告，影响里程碑追踪。

---

## 4. 下一阶段冲刺计划（2025-10 ~ 2025-11）

### Sprint A · 修复工具 & 补齐 Host Scaffold（~2025-10-18）
- 修正 `scripts/check-parity.js` 的 `cppRoot` 默认路径，恢复每日 parity 报告。
- 生成 `ReactDOMComponent/ReactDOMInstance/ReactDOMDiffProperties` 骨架并开始翻译 `diffProperties`。
- 为 DOM Host 引入最小 gtest（append/remove/insert/diff）。

### Sprint B · WorkLoop 关键路径（~2025-11-01）
- 从 `ReactFiberBeginWork.js` / `ReactFiberCompleteWork.js` / `ReactFiberCommitWork.js` 自动生成骨架并填充核心逻辑。
- 实现 `resetSuspendedWorkLoopOnUnwind`、`unwindWork`、`completeWork` 等 TODO。
- 扩展已有 `ReactFiberWorkLoopTests`，覆盖首个 host 渲染案例。

### Sprint C · Scheduler 对接（~2025-11-15）
- 翻译 `packages/scheduler` 基本实现，完成 `requestHostCallback`、`flushWork`、`advanceTimers`。
- 将 WorkLoop 渲染入口切换为 Scheduler 驱动，验证同步/过期任务处理。

---

## 5. 风险与缓解措施

- **上游变更频繁**：继续跟踪 `react-main` 更新，必要时锁定 tag 并在 parity 报告中标记新增文件。
- **JS 内建 API 差异**：统一封装在 `shared/JSMimics`，避免各文件重复实现。
- **测试缺口**：在翻译关键模块时同步补充 gtest，确保与 JS snapshot 对齐。
- **性能回归**：Phase 9 前保持语义一致为主，后续再针对热点函数做 C++ 级优化。

---

## 6. 更新指引

1. 新增翻译：
   - 在 `packages/React/src` 下创建对应 `*.h/.cpp`。
   - 更新 `ReactCppSources.cmake` 并生成/更新 `*.expect.json`。
2. 运行校验：
   - 修复后执行 `node scripts/check-parity.js --report=ci/react-parity-report.md`。
   - 运行相关 gtest（`ctest -R React`）。
3. 更新本文件：
   - 调整阶段状态/日期。
   - 在“当前完成度快照”中同步覆盖率。
   - 在“冲刺计划”中加入新任务或标记完成。

> **备注**：原 `REACT_CPP_ROADMAP.md` 保留作为详细阶段文档，本 `Roadmap.md` 用于总结现状与冲刺计划，便于周会快速对齐。
