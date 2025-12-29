# std::any → JSI DSL 类型整改 TODO

> 日期：2025-12-27

## 背景与结论

当前实现大量使用 `std::any` 承载两类完全不同的东西：

1. **开发者 DSL / JS 世界的值**：ReactElement 的 `type/props/ref`，Hooks 的 `state/action/deps`，ClassUpdateQueue 的 `payload/state` 等。
2. **宿主/渲染器句柄与内部结构**：HostConfig 的 `instance/container/hostContext`，调度回调句柄，`Fiber::updateQueue` 等类型擦除 union。

从架构上，(1) 应该逐步收敛到 `facebook::jsi::Value`；(2) 应保留为宿主句柄或进一步收敛为明确 C++ 类型。

重要约束：本仓库的 `jsi::Value` **没有拷贝构造**（只能移动；需要显式 `Value(Runtime&, const Value&)` 才能“复制/克隆”）。因此：
- **可以**：在 API 边界以 `const jsi::Value&` 传递（不持有、不复制）。
- **困难/高风险**：把 `jsi::Value` 直接存进需要 CopyConstructible 的结构（例如被放入 `std::any`、`std::vector`、作为可复制的 struct 成员）。

所以整改需要分阶段：先统一接口边界类型（传递用 `jsi::Value`），再解决“可持久化存储”的表示（要么引入 Runtime 参与克隆，要么引入 runtime-agnostic 的值表示/variant）。

---

## 目标

- 明确“DSL 值”和“宿主句柄”的类型边界，避免 `std::any` 混用。
- 逐步将 DSL 值迁移到 `jsi::Value`（优先接口边界）。
- 保持现有构建与测试全部通过（当前基线 354 tests）。

---

## 优先级分组

### P0（立刻做，低风险，高收益）

- [ ] **建立项目级类型约定**：
  - DSL 值：`jsi::Value`（边界传递为 `const jsi::Value&`）
  - Host 句柄：保持 `std::any`，后续再收敛为 HostInstance/Container 等强类型
  - 结构 union：从 `std::any` 逐步迁移为 `std::variant<...>`

- [ ] **禁止新增 DSL 用 `std::any` 的接口**：新增/修改 API 时，props/children/context/component/type 等一律用 `jsi::Value`。

### P1（开始迁移，影响中等）

- [ ] ReactChildFiber/BeginWork/CompleteWork 等边界：对 “newChild/nextChildren/props/type/context” 等 DSL 入参出参完成 `std::any → jsi::Value`。
  - 说明：这类通常只需要传引用，不需要存储。

- [ ] Hooks 边界接口：将 dispatcher 中与 JS 语义强相关的 `state/action/deps/callback` 迁移为 `jsi::Value`。

### P2（需要设计决策，影响较大）

- [ ] ReactElement/Portal 的 DSL 字段迁移：
  - 现状：[packages/React/src/react/ReactElement.h](packages/React/src/react/ReactElement.h)
  - 难点：`ReactElement`/`ReactPortal` 当前被放入 `std::any`，要求 CopyConstructible；而 `jsi::Value` 无 copy ctor。
  - 备选方案：
    - A. 引入 runtime 参与的 clone（结构内部持有 `jsi::Value`，但 copy 需要 `Runtime&`）
    - B. 引入 `ReactNodeValue`（runtime-agnostic variant：string/number/bool/null/element/array…），JSI 层与 reconciler 层之间做转换
    - C. 端到端测试中引入真实 JSI Runtime（工程量最大）

- [ ] `Fiber::updateQueue/memoizedState` 的 `std::any` 收敛为 `std::variant`：
  - 现状：[packages/React/src/reconciler/ReactFiber.h](packages/React/src/reconciler/ReactFiber.h)
  - 目标：减少 `any_cast`，把“内部结构对象”与“DSL 值”彻底分离。

---

## 文件级整改清单（初版）

### React Core

- [ ] [packages/React/src/react/ReactElement.h](packages/React/src/react/ReactElement.h)
  - `ReactElement::type/ref/props/_owner`：DSL 值（目标 `jsi::Value`），但目前受 CopyConstructible 约束阻塞
  - `ReactPortal::children`：DSL 值（目标 `jsi::Value`），同样受约束
  - ✅ 已做：`ReactPortal::key` 收敛为 `std::optional<std::string>`（不再用 `std::any`）

### Reconciler

- [ ] [packages/React/src/reconciler/ReactFiberHooks.h](packages/React/src/reconciler/ReactFiberHooks.h)
  - `state/action/deps/callback`：DSL 值（目标 `jsi::Value`）

- [ ] [packages/React/src/reconciler/ReactFiberClassUpdateQueue.h](packages/React/src/reconciler/ReactFiberClassUpdateQueue.h)
  - `payload/prevState/nextProps/instance`：DSL 值（目标 `jsi::Value`）

- [ ] [packages/React/src/reconciler/ReactFiberCompleteWork.h](packages/React/src/reconciler/ReactFiberCompleteWork.h)
  - `instance/container/hostContext`：宿主句柄（应保留 `std::any` 或收敛为 Host 类型）

---

## 落地顺序（推荐）

1. 先推进“接口边界”统一（props/children/context/component/type 等全部换成 `jsi::Value` 引用传递）
2. 再推进 Hooks（state/action/deps 统一为 `jsi::Value`），配套补测
3. 最后处理 ReactElement/Portal 的存储语义与 `std::any`/copy 约束（需要先定方案）

---

## 当前进展

- 已完成仓内扫描与分类（见最近 grep 输出）。
- 已落地一项低风险收敛：`ReactPortal::key` 从 `std::any` 改为 `std::optional<std::string>`，并同步更新 reconciler 的读取逻辑。
