# ReactJS ➜ C++ 详细转译计划（可执行版）

本计划以“可执行、可验收、可追溯”为目标设计，严格满足：
1) 模块字段 1:1（不能缺字段）
2) 流程逻辑行级别 1:1
3) 全仓库模块/函数对照表（精确到函数）
4) 每次迭代记录进度，必要时补单测

## 0. 迭代前置：冻结基准与产物约定

- 冻结 ReactJS 基准：以 `reactjs/` 当前 commit 为 SSOT。
- 每次迭代必须产出并提交：
  - `docs/reactcpp/transpilation-progress.yaml` 的新记录
  - `docs/reactcpp/generated/reactjs-function-map.json`（或在 CI 中生成并作为产物）
  - parity 报告摘要（可写入进度文件中的 `parity_summary`）

## 1. 阶段划分（建议 6 个阶段，循序渐进）

### Phase A：清单化与可见性（1～2 次迭代）

目标：把“全量模块/函数清单 + 可重复生成 + 缺口可量化”先建立起来。

交付：
- `scripts/translate-react.js` 能生成：
  - 全仓库模块清单
  - 全仓库函数清单（精确到函数名与行号）
- `scripts/check-parity.js` 至少能做字段 parity（先从 `Fiber` 开始）。

验收：
- 任意一次 `node scripts/translate-react.js` 运行结果可重现。
- parity 输出能定位到：模块路径 + 缺失字段名。

### Phase B：核心数据结构字段 1:1（2～4 次迭代）

目标：优先把 `Fiber`/`FiberRoot`/UpdateQueue 等结构字段对齐，这是后续 100% 行级转译的基础。

做法：
- 以 ReactJS `react-reconciler/src/ReactInternalTypes.js` 为源，逐个结构对齐：
  1) `Fiber`
  2) `FiberRoot`
  3) `Dependencies`/`ContextDependency`
  4) UpdateQueue 相关结构
- 通过 parity 工具强制约束：缺字段 = 失败。

验收：
- 字段 parity：缺失字段数趋近 0（DEV-only 可用宏包裹但不能漏）。
- 新增至少一组 gtest 验证字段默认值与关键不变量。

### Phase C：Shared/Flags/Tags 常量域（1～2 次迭代）

目标：把 `WorkTags`/`FiberFlags`/`Lanes`/FeatureFlags 等“数值域”对齐，减少后续逻辑偏差。

验收：
- gtest 断言关键常量值与 JS snapshot 一致。

### Phase D：WorkLoop（同步路径）行级别对齐（多次迭代）

目标：以 WorkLoop 为主干，把 beginWork/completeWork/commit 的关键路径跑通。

要求：
- 每个函数都必须带源追踪注释（JS 路径+行号）。
- 每次迭代只承诺一组闭环：新增/转译函数 + 单测覆盖 + parity 过。

验收：
- WorkLoop 关键函数集合逐步从“缺失”变成“存在且行为受测”。

### Phase E：Hooks/Context（多次迭代）

目标：Dispatcher + hook 链表 + effect list 对齐；逐 hook 引入单测。

### Phase F：Scheduler/HostConfig/DOM（按产品需要推进）

目标：按宿主实现推进调度与 DOM host config。

## 2. 每次迭代（Iteration）的标准工作流

> 这是你要求的“每次迭代都记录进度 + 必要时单测”的具体可执行流程。

1) **选择范围**（尽量小）：1～3 个 JS 模块 or 5～15 个函数。
2) **生成对照表快照**：运行 `node scripts/translate-react.js --out docs/reactcpp/generated`。
3) **字段对齐（若涉及结构体）**：先补齐字段再做逻辑。
4) **行级转译**：逐函数翻译，保持语句顺序；每个函数加源追踪注释。
5) **补单测**：
   - 结构/常量：新增 gtest
   - 行为：补 WorkLoop/Hooks 的路径测试
6) **跑 parity**：`node scripts/check-parity.js`，确保：
   - 该迭代承诺范围内字段缺失 = 0
   - 函数对照可定位（至少能从注释或符号名映射）
7) **更新进度文件**：追加一条 iteration 记录（见 progress 文件模板）。

## 3. Definition of Done（DoD）

对“一个函数已转译完成”的 DoD：
- C++ 中存在同名函数（或映射表中声明的等价符号）。
- 存在源追踪注释（JS 路径 + 行号范围）。
- 控制流结构与 JS 一致（由 reviewer + parity 辅助校验）。
- 覆盖至少一个单测（若为纯 glue/薄封装可豁免，但必须在进度记录中说明原因）。

对“一个模块已完成”的 DoD：
- 模块内顶层函数全部在对照表中标记为已转译。
- 若模块定义了关键结构字段：字段 parity 为 0。

