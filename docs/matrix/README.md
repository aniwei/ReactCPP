# ReactCPP 转译项目文档

本目录包含 ReactJS → JSI C++ 转译项目的核心文档。

## 📁 目录结构

```
docs/
├── reactcpp/                          # 转译项目文档
│   ├── JSI_React_Transpilation_TechSpec.md   # 技术方案
│   ├── JSI_React_Transpilation_Plan.md       # 详细转译计划
│   └── generated/                     # 自动生成的文档
│
└── matrix/                            # 模块对照与进度追踪
    ├── README.md                      # 本文件
    ├── progress-log.md                # 每日进度日志
    ├── react-function-map.jsonl       # 函数级对照表 (JSONL)
    ├── react-exports-map.json         # 导出映射表 (JSON)
    ├── react-module-map.csv           # 模块映射表 (CSV)
    └── parse-errors.jsonl             # 解析错误日志
```

## 📄 文件说明

### 核心文档

| 文件 | 说明 |
|------|------|
| [JSI_React_Transpilation_TechSpec.md](../reactcpp/JSI_React_Transpilation_TechSpec.md) | 完整的技术方案，包含架构设计、转译规则、JSI 集成规范 |
| [JSI_React_Transpilation_Plan.md](../reactcpp/JSI_React_Transpilation_Plan.md) | 详细的转译计划，包含阶段划分、任务列表、进度跟踪 |

### 对照表文件

| 文件 | 格式 | 说明 |
|------|------|------|
| `react-function-map.jsonl` | JSONL | 精确到函数级别的 JS → C++ 映射表 |
| `react-exports-map.json` | JSON | 每个源文件的导出符号列表 |
| `react-module-map.csv` | CSV | 便于表格查看的模块映射 |
| `parse-errors.jsonl` | JSONL | 解析失败的文件记录 |

### 进度追踪

| 文件 | 说明 |
|------|------|
| `progress-log.md` | 每日进度记录、里程碑追踪、风险管理 |

## 🔧 生成对照表

运行以下命令更新对照表：

```bash
# 生成所有核心包的对照表
npm run generate-mapping

# 仅生成指定包
node scripts/generate-react-mapping.js --packages react,scheduler

# 详细输出
node scripts/generate-react-mapping.js --verbose

# 仅显示统计信息
node scripts/generate-react-mapping.js --stats
```

## 📊 对照表字段说明

### react-function-map.jsonl

每行一个 JSON 对象，字段如下：

```json
{
  "sourceFile": "reactjs/packages/react/src/ReactHooks.js",
  "sourceStartLine": 45,
  "sourceEndLine": 52,
  "symbolKind": "function",
  "symbolName": "useState",
  "isExported": true,
  "cppTargetFile": "packages/React/src/react/ReactHooks.cpp",
  "cppSymbol": "react::core::useState",
  "status": "not-started",
  "testRequired": true
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `sourceFile` | string | ReactJS 源文件相对路径 |
| `sourceStartLine` | number | 符号起始行号 |
| `sourceEndLine` | number | 符号结束行号 |
| `symbolKind` | string | 符号类型：function, arrow-fn, class, class-method, object-method, variable-export, const-export, type-export |
| `symbolName` | string | 符号名称 |
| `isExported` | boolean | 是否为导出符号 |
| `cppTargetFile` | string | C++ 目标文件路径 |
| `cppSymbol` | string | C++ 完整符号名（含命名空间） |
| `status` | string | 转译状态：not-started, in-progress, completed, blocked |
| `testRequired` | boolean | 是否需要单元测试 |

### 状态说明

| 状态 | 图标 | 说明 |
|------|------|------|
| `not-started` | ⚪ | 未开始 |
| `in-progress` | 🟡 | 进行中 |
| `completed` | 🟢 | 已完成 |
| `blocked` | 🔴 | 被阻塞 |

## 📈 快速查看进度

```bash
# 查看总体进度
grep -c '"status":"completed"' docs/matrix/react-function-map.jsonl

# 查看各状态统计
cat docs/matrix/react-function-map.jsonl | \
  jq -s 'group_by(.status) | map({status: .[0].status, count: length})'

# 查看特定模块进度
grep 'react-reconciler' docs/matrix/react-function-map.jsonl | \
  jq -s 'group_by(.status) | map({status: .[0].status, count: length})'
```

## 🔗 相关链接

- [ReactJS 源码](https://github.com/facebook/react)
- [JSI 文档](https://reactnative.dev/docs/the-new-architecture/landing-page)
- [项目 Roadmap](../../REACT_CPP_ROADMAP.md)

---

*最后更新: 2025-12-27*
