# ReactJS ➜ C++（JSI/Wasm）1:1 转译技术方案

> 目标：以本仓库内的 ReactJS 源码镜像（`reactjs/`）为单一事实来源（SSOT），在 C++ 侧按“模块字段 1:1、流程逻辑行级别 1:1、函数级别可追溯”的方式逐步完成移植；任何偏差必须能在工具报告中定位到具体模块/函数/行号。

## 1. 范围与术语

- **ReactJS 源**：本仓库目录 `reactjs/` 下的 upstream React monorepo 快照。
- **C++ 目标**：本仓库 `packages/React/` 以及其下的 C++ 代码与测试（gtest）。
- **模块（Module）**：以 ReactJS 文件为单位（例如 `reactjs/packages/react-reconciler/src/ReactFiberWorkLoop.js`）。
- **函数（Function）**：ReactJS 模块内的顶层函数/导出函数（含 `function`、`const foo = () => {}` 等）。
- **字段（Field）**：ReactJS 中 Flow 类型的 object 字段（例如 `ReactInternalTypes.js` 的 `Fiber` type）。

## 2. 转译规则（必须遵守）

### 2.1 模块字段 1:1（不能缺少字段）

- 对应关系：当 ReactJS 定义了关键结构（典型：`Fiber`、`FiberRoot`、UpdateQueue 等），C++ 必须存在等价结构体/类，并且字段集合**不缺失**。
- 字段对齐策略：
  - ReactJS Flow object type 的每个字段必须在 C++ 中出现（允许通过 `#if REACTCPP_DEV` 等编译开关包裹 DEV-only 字段，但字段名本身不能“消失”）。
  - 对于 ReactJS `any` / `mixed` 等宽类型，C++ 侧采用 `facebook::jsi::Value` 或明确的宿主接口类型作为承载；原则是“保持语义，不做类型收窄带来的行为变化”。
- 工具约束：`node scripts/check-parity.js` 必须能从 ReactJS 源中抽取字段列表，并对照 C++ 结构体字段，输出缺失字段清单。

### 2.2 流程逻辑 100% 行级别转译

- **逐语句对齐**：C++ 实现必须按 ReactJS 源码语句顺序重写，不允许为了“更 C++”而重排控制流。
- **控制流等价**：`if/else`、`switch`、`for/while`、`try/catch/finally`、短路逻辑（`&&`/`||`/`?:`）必须保持等价。
- **禁止随意重构**：
  - 仅在 JS 依赖宿主/运行时 API（例如 `Map`、`WeakMap`、`performance.now`）时，允许引入最小等价封装。
  - 不允许将多个 JS 函数“合并”为一个 C++ 函数；不允许把一个 JS 函数拆成多个 C++ 函数（除非拆分后的每个函数都有明确的 1:1 对应并在映射表中记录）。
- **行号可追溯**：每个被转译的 C++ 函数应包含源追踪注释（推荐格式）：
  - `// ReactJS: reactjs/packages/.../SomeFile.js#L123-L240  function: foo`
  - 该注释用于后续 parity/审计工具建立“函数级别对照”。

### 2.3 全仓库模块/函数对照表（精确到函数）

- 必须能从 `reactjs/` 源码**自动生成**：
  - 列出每个模块的所有函数（至少包含模块级顶层函数；若某模块以 class 方式组织，则列出 class method）。
  - 每个函数必须包含：模块路径、函数名、起止行号、是否导出（export）。
- 必须能在对照表中体现 C++ 侧的“计划/落地位置”：
  - 至少提供 C++ 目标路径规则（见 §3），以及可选的 `cpp_file`/`cpp_symbol`（若已落地）。
- 工具入口：`node scripts/translate-react.js`（生成映射表与骨架/清单）。

### 2.4 迭代进度记录 + 必要时单元测试

- 每次迭代（一个 PR/一个 Sprint）必须：
  1) 更新进度文件（见 [docs/reactcpp/transpilation-progress.yaml](transpilation-progress.yaml)）。
  2) 运行 parity 工具并记录结果（字段缺失数、函数缺失数等）。
  3) 如涉及核心行为（WorkLoop、Hooks、Commit、Scheduler），必须新增/更新对应 gtest 单测。

## 3. 目录/命名映射（建议规范）

> 原则：文件名保持 1:1 可读性；目录按域分层（ReactReconciler / ReactDOM / shared / Scheduler / ReactRuntime）。

- ReactJS 模块路径：`reactjs/packages/<pkg>/src/<path>/<File>.js`
- C++ 目标建议路径：`packages/React/src/<Domain>/<File>.{h,cpp}`
  - `<Domain>` 由 `<pkg>` 决定：
    - `react-reconciler` ➜ `ReactReconciler/`
    - `react-dom`/`react-dom-bindings` ➜ `ReactDOM/`
    - `shared` ➜ `shared/`
    - `scheduler` ➜ `Scheduler/`（或 `scheduler/`，但需统一）
    - `react` ➜ `ReactCore/`（或 `React/`，但需统一）

> 注意：本仓库当前存在“目录不齐/脚本缺失”的状态，建议以本规范为目标并在计划中逐步对齐。

## 4. 工具链设计（确保规则可执行）

### 4.1 `scripts/translate-react.js`（生成清单/对照表）

职责：
- 扫描 `reactjs/` 全部源码，生成：
  - 模块清单（modules）
  - 函数清单（functions，精确到函数与行号）
  - 可选：按映射规则推导的 C++ 目标路径列
- 输出到：`docs/reactcpp/generated/`（默认）

### 4.2 `scripts/check-parity.js`（字段/函数 parity 校验）

职责：
- **字段 parity**：以 `ReactInternalTypes.js` 等为源，抽取结构字段；对照 C++ `struct` 字段；报出缺失字段。
- **函数 parity（阶段性）**：
  - 基线：对照“已转译模块列表”中的函数，检查 C++ 侧是否存在同名符号（可先用启发式 grep；当 C++ 侧采用 §2.2 的源追踪注释后，可升级为精确映射）。

## 5. 单元测试策略

- 优先在 `packages/React/test/` 下新增 gtest：
  - 常量/flag：数值一致性测试（WorkTags/FiberFlags/Lanes）。
  - WorkLoop：lane 选择、调度分支、错误恢复路径。
  - Hooks：dispatcher 行为、memoizedState 链、effect list。
- **必要时**：引入“对照测试”——同一输入分别喂给 JS 参考实现与 C++ 实现，比较关键中间状态（如 lane、flags、fiber tree 形状）。

## 6. 交付物（你要的 4 条规则对应落地）

- 技术方案文档：本文件。
- 详细转译计划：见 [docs/reactcpp/reactjs-cpp-transpilation-plan.md](reactjs-cpp-transpilation-plan.md)。
- 模块/函数对照表：由 `node scripts/translate-react.js` 生成到 `docs/reactcpp/generated/`。
- 迭代进度记录：见 [docs/reactcpp/transpilation-progress.yaml](transpilation-progress.yaml)。
