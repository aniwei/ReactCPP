# ReactCPP Scripts

本目录包含 ReactJS → JSI C++ 转译项目的工具脚本。

## 📁 脚本列表

| 脚本 | 说明 |
|------|------|
| `generate-react-mapping.js` | 生成模块和函数对照表 |

## 🔧 generate-react-mapping.js

### 功能

解析 ReactJS 源码，生成精确到函数级别的对照表，用于：

1. 追踪转译进度
2. 验证翻译完整性
3. 生成 C++ 代码骨架

### 使用方法

```bash
# 基本使用 - 生成核心包对照表
npm run generate-mapping

# 或直接运行
node scripts/generate-react-mapping.js

# 指定包
node scripts/generate-react-mapping.js --packages react,scheduler,shared

# 详细输出
node scripts/generate-react-mapping.js --verbose

# 仅显示统计信息
node scripts/generate-react-mapping.js --stats
```

### 输出文件

| 文件 | 格式 | 说明 |
|------|------|------|
| `docs/matrix/react-function-map.jsonl` | JSONL | 函数级对照表 |
| `docs/matrix/react-exports-map.json` | JSON | 导出映射表 |
| `docs/matrix/react-module-map.csv` | CSV | 模块映射表 |
| `docs/matrix/parse-errors.jsonl` | JSONL | 解析错误日志 |

### 依赖

需要安装以下 npm 包：

```bash
npm install @babel/parser @babel/traverse
```

### 配置

脚本内置配置可在 `CONFIG` 对象中修改：

```javascript
const CONFIG = {
    // ReactJS 源码路径
    reactJsPath: path.join(__dirname, '../reactjs/packages'),
    
    // 输出目录
    outputDir: path.join(__dirname, '../docs/matrix'),
    
    // C++ 目标根目录
    cppTargetRoot: 'packages/React/src',
    
    // 要处理的核心包
    corePackages: [
        'react',
        'react-reconciler', 
        'scheduler',
        'shared',
    ],
    // ...
};
```

## 📝 添加新脚本

新脚本应遵循以下规范：

1. 文件头部添加功能说明和使用方法
2. 支持 `--help` 参数
3. 输出使用 emoji 前缀提高可读性
4. 错误输出到 stderr
5. 返回适当的退出码

---

*最后更新: 2025-12-27*
