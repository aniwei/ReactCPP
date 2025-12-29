#!/usr/bin/env node
/**
 * ReactJS 模块和函数对照表生成脚本
 * 
 * 功能：
 * 1. 解析 ReactJS 源码，提取所有模块、函数、常量
 * 2. 生成精确到函数级别的对照表
 * 3. 输出 JSONL 格式的映射文件
 * 4. 支持状态跟踪和进度统计
 * 
 * 使用方法：
 *   npm run generate-mapping
 *   node scripts/generate-react-mapping.js [options]
 * 
 * 选项：
 *   --packages <list>   指定要处理的包，逗号分隔
 *   --output <dir>      输出目录，默认 docs/matrix
 *   --verbose           详细输出
 *   --stats             仅输出统计信息
 */

const fs = require('fs');
const path = require('path');
const parser = require('@babel/parser');
const traverse = require('@babel/traverse').default;

// ============================================================================
// 配置
// ============================================================================

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
    
    // 可选处理的包
    optionalPackages: [
        'react-dom',
        'react-dom-bindings',
        'react-is',
    ],
    
    // 排除的包
    excludePackages: [
        'react-devtools',
        'react-devtools-core',
        'react-devtools-extensions',
        'react-devtools-inline',
        'react-devtools-shared',
        'react-devtools-shell',
        'react-devtools-timeline',
        'react-devtools-fusebox',
        'eslint-plugin-react-hooks',
        'dom-event-testing-library',
        'internal-test-utils',
        'jest-react',
    ],
    
    // 排除的目录模式
    excludeDirs: [
        '__tests__',
        '__mocks__',
        'test',
        'tests',
        'forks',  // 保留，但需要特殊处理
    ],
    
    // 排除的文件模式
    excludeFiles: [
        /\.test\./,
        /\.spec\./,
        /Test\.js$/,
        /\.d\.ts$/,
    ],
};

// ============================================================================
// 符号类型定义
// ============================================================================

const SymbolKind = {
    FUNCTION: 'function',
    ARROW_FN: 'arrow-fn',
    CLASS: 'class',
    CLASS_METHOD: 'class-method',
    OBJECT_METHOD: 'object-method',
    VARIABLE_EXPORT: 'variable-export',
    CONST_EXPORT: 'const-export',
    TYPE_EXPORT: 'type-export',
    ENUM: 'enum',
};

// ============================================================================
// 工具函数
// ============================================================================

/**
 * 获取相对于 workspace 的路径
 */
function getRelativePath(absolutePath) {
    const workspaceRoot = path.join(__dirname, '..');
    return path.relative(workspaceRoot, absolutePath);
}

/**
 * 生成 C++ 目标文件路径
 */
function generateCppTargetPath(jsSourcePath) {
    // reactjs/packages/react/src/ReactHooks.js 
    // -> packages/React/src/react/ReactHooks.cpp
    
    const relativePath = jsSourcePath.replace('reactjs/packages/', '');
    const parts = relativePath.split('/');
    const packageName = parts[0];
    
    // 构建 C++ 路径
    let cppPath = CONFIG.cppTargetRoot + '/';
    
    if (parts[1] === 'src') {
        cppPath += packageName + '/' + parts.slice(2).join('/');
    } else {
        cppPath += packageName + '/' + parts.slice(1).join('/');
    }
    
    // 替换扩展名
    cppPath = cppPath.replace(/\.js$/, '.cpp');
    
    return cppPath;
}

/**
 * 生成 C++ 符号名（含命名空间）
 */
function generateCppSymbol(packageName, symbolName) {
    const namespaceMap = {
        'react': 'react::core',
        'react-reconciler': 'react::reconciler',
        'scheduler': 'react::scheduler',
        'shared': 'react::shared',
        'react-dom': 'react::dom',
        'react-dom-bindings': 'react::dom::bindings',
    };
    
    const ns = namespaceMap[packageName] || 'react';
    return `${ns}::${symbolName}`;
}

/**
 * 解析 JavaScript/TypeScript 文件
 */
function parseFile(filePath) {
    const content = fs.readFileSync(filePath, 'utf-8');
    
    try {
        return parser.parse(content, {
            sourceType: 'module',
            plugins: [
                'jsx',
                'flow',
                'classProperties',
                'classPrivateProperties',
                'classPrivateMethods',
                'dynamicImport',
                'exportDefaultFrom',
                'exportNamespaceFrom',
                'optionalChaining',
                'nullishCoalescingOperator',
            ],
        });
    } catch (error) {
        // 解析失败时返回 null
        return null;
    }
}

// ============================================================================
// AST 遍历与符号提取
// ============================================================================

/**
 * 从 AST 中提取符号信息
 */
function extractSymbols(ast, sourceFile, packageName) {
    const symbols = [];
    
    if (!ast) return symbols;
    
    traverse(ast, {
        // 导出声明
        ExportNamedDeclaration(nodePath) {
            const { node } = nodePath;
            
            if (node.declaration) {
                // export function xxx() {}
                // export const xxx = ...
                // export class xxx {}
                extractDeclarationSymbols(node.declaration, true, symbols, sourceFile, packageName);
            }
            
            if (node.specifiers) {
                // export { xxx, yyy }
                for (const specifier of node.specifiers) {
                    if (specifier.type === 'ExportSpecifier') {
                        symbols.push({
                            sourceFile,
                            sourceStartLine: specifier.loc?.start?.line || 0,
                            sourceEndLine: specifier.loc?.end?.line || 0,
                            symbolKind: 'variable-export',
                            symbolName: specifier.exported.name,
                            isExported: true,
                            cppTargetFile: generateCppTargetPath(sourceFile),
                            cppSymbol: generateCppSymbol(packageName, specifier.exported.name),
                            status: 'not-started',
                            testRequired: true,
                        });
                    }
                }
            }
        },
        
        // 默认导出
        ExportDefaultDeclaration(nodePath) {
            const { node } = nodePath;
            
            if (node.declaration) {
                if (node.declaration.type === 'Identifier') {
                    symbols.push({
                        sourceFile,
                        sourceStartLine: node.loc?.start?.line || 0,
                        sourceEndLine: node.loc?.end?.line || 0,
                        symbolKind: 'default-export',
                        symbolName: node.declaration.name,
                        isExported: true,
                        cppTargetFile: generateCppTargetPath(sourceFile),
                        cppSymbol: '',
                        status: 'not-started',
                        testRequired: false,
                    });
                } else {
                    extractDeclarationSymbols(node.declaration, true, symbols, sourceFile, packageName);
                }
            }
        },
        
        // 函数声明（非导出）
        FunctionDeclaration(nodePath) {
            const { node, parent } = nodePath;
            
            // 跳过已处理的导出
            if (parent.type === 'ExportNamedDeclaration' || 
                parent.type === 'ExportDefaultDeclaration') {
                return;
            }
            
            if (node.id) {
                symbols.push({
                    sourceFile,
                    sourceStartLine: node.loc?.start?.line || 0,
                    sourceEndLine: node.loc?.end?.line || 0,
                    symbolKind: SymbolKind.FUNCTION,
                    symbolName: node.id.name,
                    isExported: false,
                    cppTargetFile: generateCppTargetPath(sourceFile),
                    cppSymbol: '',
                    status: 'not-started',
                    testRequired: false,
                });
            }
        },
        
        // 变量声明
        VariableDeclaration(nodePath) {
            const { node, parent } = nodePath;
            
            // 跳过已处理的导出
            if (parent.type === 'ExportNamedDeclaration') {
                return;
            }
            
            for (const declarator of node.declarations) {
                if (declarator.id.type === 'Identifier' && declarator.init) {
                    const symbolKind = getSymbolKindFromInit(declarator.init);
                    
                    if (symbolKind) {
                        symbols.push({
                            sourceFile,
                            sourceStartLine: declarator.loc?.start?.line || 0,
                            sourceEndLine: declarator.init.loc?.end?.line || declarator.loc?.end?.line || 0,
                            symbolKind,
                            symbolName: declarator.id.name,
                            isExported: false,
                            cppTargetFile: generateCppTargetPath(sourceFile),
                            cppSymbol: '',
                            status: 'not-started',
                            testRequired: false,
                        });
                    }
                }
            }
        },
        
        // 类声明
        ClassDeclaration(nodePath) {
            const { node, parent } = nodePath;
            
            if (parent.type === 'ExportNamedDeclaration' || 
                parent.type === 'ExportDefaultDeclaration') {
                return;
            }
            
            if (node.id) {
                symbols.push({
                    sourceFile,
                    sourceStartLine: node.loc?.start?.line || 0,
                    sourceEndLine: node.loc?.end?.line || 0,
                    symbolKind: SymbolKind.CLASS,
                    symbolName: node.id.name,
                    isExported: false,
                    cppTargetFile: generateCppTargetPath(sourceFile),
                    cppSymbol: '',
                    status: 'not-started',
                    testRequired: false,
                });
                
                // 提取类方法
                extractClassMethods(node, symbols, sourceFile, packageName, false);
            }
        },
    });
    
    return symbols;
}

/**
 * 从声明中提取符号
 */
function extractDeclarationSymbols(declaration, isExported, symbols, sourceFile, packageName) {
    switch (declaration.type) {
        case 'FunctionDeclaration':
            if (declaration.id) {
                symbols.push({
                    sourceFile,
                    sourceStartLine: declaration.loc?.start?.line || 0,
                    sourceEndLine: declaration.loc?.end?.line || 0,
                    symbolKind: SymbolKind.FUNCTION,
                    symbolName: declaration.id.name,
                    isExported,
                    cppTargetFile: generateCppTargetPath(sourceFile),
                    cppSymbol: isExported ? generateCppSymbol(packageName, declaration.id.name) : '',
                    status: 'not-started',
                    testRequired: isExported,
                });
            }
            break;
            
        case 'VariableDeclaration':
            for (const declarator of declaration.declarations) {
                if (declarator.id.type === 'Identifier') {
                    const symbolKind = getSymbolKindFromInit(declarator.init) || 'variable-export';
                    
                    symbols.push({
                        sourceFile,
                        sourceStartLine: declarator.loc?.start?.line || 0,
                        sourceEndLine: declarator.init?.loc?.end?.line || declarator.loc?.end?.line || 0,
                        symbolKind,
                        symbolName: declarator.id.name,
                        isExported,
                        cppTargetFile: generateCppTargetPath(sourceFile),
                        cppSymbol: isExported ? generateCppSymbol(packageName, declarator.id.name) : '',
                        status: 'not-started',
                        testRequired: isExported,
                    });
                    
                    // 如果是对象字面量，提取方法
                    if (declarator.init?.type === 'ObjectExpression') {
                        extractObjectMethods(declarator.init, declarator.id.name, symbols, sourceFile, packageName, isExported);
                    }
                }
            }
            break;
            
        case 'ClassDeclaration':
            if (declaration.id) {
                symbols.push({
                    sourceFile,
                    sourceStartLine: declaration.loc?.start?.line || 0,
                    sourceEndLine: declaration.loc?.end?.line || 0,
                    symbolKind: SymbolKind.CLASS,
                    symbolName: declaration.id.name,
                    isExported,
                    cppTargetFile: generateCppTargetPath(sourceFile),
                    cppSymbol: isExported ? generateCppSymbol(packageName, declaration.id.name) : '',
                    status: 'not-started',
                    testRequired: isExported,
                });
                
                extractClassMethods(declaration, symbols, sourceFile, packageName, isExported);
            }
            break;
            
        case 'TypeAlias':
        case 'OpaqueType':
        case 'InterfaceDeclaration':
            symbols.push({
                sourceFile,
                sourceStartLine: declaration.loc?.start?.line || 0,
                sourceEndLine: declaration.loc?.end?.line || 0,
                symbolKind: SymbolKind.TYPE_EXPORT,
                symbolName: declaration.id?.name || 'unknown',
                isExported,
                cppTargetFile: generateCppTargetPath(sourceFile).replace('.cpp', '.h'),
                cppSymbol: '',
                status: 'not-started',
                testRequired: false,
            });
            break;
    }
}

/**
 * 根据初始化表达式确定符号类型
 */
function getSymbolKindFromInit(init) {
    if (!init) return null;
    
    switch (init.type) {
        case 'ArrowFunctionExpression':
            return SymbolKind.ARROW_FN;
        case 'FunctionExpression':
            return SymbolKind.FUNCTION;
        case 'ObjectExpression':
            return 'variable-export';
        case 'NumericLiteral':
        case 'StringLiteral':
        case 'BooleanLiteral':
            return 'const-export';
        case 'BinaryExpression':
            // 位运算常量
            return 'const-export';
        default:
            return null;
    }
}

/**
 * 提取类方法
 */
function extractClassMethods(classNode, symbols, sourceFile, packageName, isExported) {
    if (!classNode.body || !classNode.body.body) return;
    
    const className = classNode.id?.name || 'AnonymousClass';
    
    for (const member of classNode.body.body) {
        if (member.type === 'ClassMethod' || member.type === 'ClassPrivateMethod') {
            const methodName = member.key.name || member.key.id?.name || 'unknown';
            
            symbols.push({
                sourceFile,
                sourceStartLine: member.loc?.start?.line || 0,
                sourceEndLine: member.loc?.end?.line || 0,
                symbolKind: SymbolKind.CLASS_METHOD,
                symbolName: `${className}.${methodName}`,
                isExported: false,
                cppTargetFile: generateCppTargetPath(sourceFile),
                cppSymbol: '',
                status: 'not-started',
                testRequired: false,
            });
        }
    }
}

/**
 * 提取对象方法
 */
function extractObjectMethods(objectNode, objectName, symbols, sourceFile, packageName, isExported) {
    for (const prop of objectNode.properties) {
        if (prop.type === 'ObjectMethod' || 
            (prop.type === 'ObjectProperty' && 
             (prop.value?.type === 'FunctionExpression' || prop.value?.type === 'ArrowFunctionExpression'))) {
            
            const methodName = prop.key.name || prop.key.value || 'unknown';
            
            symbols.push({
                sourceFile,
                sourceStartLine: prop.loc?.start?.line || 0,
                sourceEndLine: prop.loc?.end?.line || 0,
                symbolKind: SymbolKind.OBJECT_METHOD,
                symbolName: methodName,
                isExported: false,
                cppTargetFile: generateCppTargetPath(sourceFile),
                cppSymbol: '',
                status: 'not-started',
                testRequired: false,
            });
        }
    }
}

// ============================================================================
// 文件遍历
// ============================================================================

/**
 * 递归获取目录下所有 JS 文件
 */
function getJsFiles(dir, files = []) {
    if (!fs.existsSync(dir)) {
        return files;
    }
    
    const entries = fs.readdirSync(dir, { withFileTypes: true });
    
    for (const entry of entries) {
        const fullPath = path.join(dir, entry.name);
        
        if (entry.isDirectory()) {
            // 检查是否是排除目录
            if (CONFIG.excludeDirs.includes(entry.name)) {
                continue;
            }
            getJsFiles(fullPath, files);
        } else if (entry.isFile()) {
            // 检查是否是 JS 文件
            if (!entry.name.endsWith('.js')) {
                continue;
            }
            
            // 检查是否匹配排除模式
            const shouldExclude = CONFIG.excludeFiles.some(pattern => pattern.test(entry.name));
            if (shouldExclude) {
                continue;
            }
            
            files.push(fullPath);
        }
    }
    
    return files;
}

/**
 * 处理单个包
 */
function processPackage(packageName) {
    const packagePath = path.join(CONFIG.reactJsPath, packageName);
    const srcPath = path.join(packagePath, 'src');
    
    // 优先使用 src 目录，否则使用包根目录
    const targetPath = fs.existsSync(srcPath) ? srcPath : packagePath;
    
    const jsFiles = getJsFiles(targetPath);
    const allSymbols = [];
    const exports = [];
    const errors = [];
    
    for (const filePath of jsFiles) {
        const relativePath = getRelativePath(filePath);
        
        try {
            const ast = parseFile(filePath);
            
            if (ast) {
                const symbols = extractSymbols(ast, relativePath, packageName);
                allSymbols.push(...symbols);
                
                // 收集导出信息
                const fileExports = symbols.filter(s => s.isExported).map(s => s.symbolName);
                if (fileExports.length > 0) {
                    exports.push({
                        sourceFile: relativePath,
                        exports: fileExports,
                        hasDefaultExport: symbols.some(s => s.symbolKind === 'default-export'),
                        hasExportAll: false, // TODO: 检测 export *
                    });
                }
            } else {
                errors.push({
                    file: relativePath,
                    error: 'Failed to parse file',
                });
            }
        } catch (error) {
            errors.push({
                file: relativePath,
                error: error.message,
            });
        }
    }
    
    return { symbols: allSymbols, exports, errors };
}

// ============================================================================
// 输出生成
// ============================================================================

/**
 * 生成 JSONL 格式的函数映射表
 */
function generateFunctionMap(symbols, outputPath) {
    const lines = symbols.map(s => JSON.stringify(s)).join('\n');
    fs.writeFileSync(outputPath, lines);
    console.log(`✅ Generated function map: ${outputPath}`);
    console.log(`   Total symbols: ${symbols.length}`);
}

/**
 * 生成 JSON 格式的导出映射表
 */
function generateExportsMap(exports, outputPath) {
    fs.writeFileSync(outputPath, JSON.stringify(exports, null, 2));
    console.log(`✅ Generated exports map: ${outputPath}`);
    console.log(`   Total files: ${exports.length}`);
}

/**
 * 生成 CSV 格式的模块映射表
 */
function generateModuleMap(symbols, outputPath) {
    const header = 'sourceFile,symbolName,symbolKind,isExported,cppTargetFile,status';
    const rows = symbols.map(s => 
        `"${s.sourceFile}","${s.symbolName}","${s.symbolKind}",${s.isExported},"${s.cppTargetFile}","${s.status}"`
    );
    fs.writeFileSync(outputPath, [header, ...rows].join('\n'));
    console.log(`✅ Generated module map: ${outputPath}`);
}

/**
 * 生成错误日志
 */
function generateErrorLog(errors, outputPath) {
    const lines = errors.map(e => JSON.stringify(e)).join('\n');
    fs.writeFileSync(outputPath, lines);
    if (errors.length > 0) {
        console.log(`⚠️ Generated error log: ${outputPath}`);
        console.log(`   Total errors: ${errors.length}`);
    }
}

/**
 * 输出统计信息
 */
function printStats(symbols) {
    console.log('\n📊 Statistics:');
    
    // 按包统计
    const byPackage = {};
    for (const s of symbols) {
        const pkg = s.sourceFile.split('/')[2]; // reactjs/packages/xxx
        byPackage[pkg] = byPackage[pkg] || { total: 0, exported: 0, functions: 0 };
        byPackage[pkg].total++;
        if (s.isExported) byPackage[pkg].exported++;
        if (s.symbolKind === 'function' || s.symbolKind === 'arrow-fn') byPackage[pkg].functions++;
    }
    
    console.log('\n   By Package:');
    for (const [pkg, stats] of Object.entries(byPackage).sort((a, b) => b[1].total - a[1].total)) {
        console.log(`   - ${pkg}: ${stats.total} symbols (${stats.exported} exported, ${stats.functions} functions)`);
    }
    
    // 按类型统计
    const byKind = {};
    for (const s of symbols) {
        byKind[s.symbolKind] = (byKind[s.symbolKind] || 0) + 1;
    }
    
    console.log('\n   By Kind:');
    for (const [kind, count] of Object.entries(byKind).sort((a, b) => b[1] - a[1])) {
        console.log(`   - ${kind}: ${count}`);
    }
    
    // 按状态统计
    const byStatus = {};
    for (const s of symbols) {
        byStatus[s.status] = (byStatus[s.status] || 0) + 1;
    }
    
    console.log('\n   By Status:');
    for (const [status, count] of Object.entries(byStatus)) {
        console.log(`   - ${status}: ${count}`);
    }
}

// ============================================================================
// 主入口
// ============================================================================

async function main() {
    console.log('🚀 ReactJS Module & Function Mapping Generator\n');
    
    // 解析命令行参数
    const args = process.argv.slice(2);
    const verbose = args.includes('--verbose');
    const statsOnly = args.includes('--stats');
    
    // 确定要处理的包
    let packages = [...CONFIG.corePackages];
    const pkgIndex = args.indexOf('--packages');
    if (pkgIndex !== -1 && args[pkgIndex + 1]) {
        packages = args[pkgIndex + 1].split(',');
    }
    
    // 确保输出目录存在
    if (!fs.existsSync(CONFIG.outputDir)) {
        fs.mkdirSync(CONFIG.outputDir, { recursive: true });
    }
    
    const allSymbols = [];
    const allExports = [];
    const allErrors = [];
    
    // 处理每个包
    for (const pkg of packages) {
        if (verbose) console.log(`\n📦 Processing package: ${pkg}`);
        
        const { symbols, exports, errors } = processPackage(pkg);
        
        allSymbols.push(...symbols);
        allExports.push(...exports);
        allErrors.push(...errors);
        
        if (verbose) {
            console.log(`   Found ${symbols.length} symbols, ${exports.length} export entries`);
            if (errors.length > 0) {
                console.log(`   ⚠️ ${errors.length} parse errors`);
            }
        }
    }
    
    // 输出统计
    printStats(allSymbols);
    
    if (statsOnly) {
        return;
    }
    
    // 生成输出文件
    console.log('\n📄 Generating output files...\n');
    
    generateFunctionMap(allSymbols, path.join(CONFIG.outputDir, 'react-function-map.jsonl'));
    generateExportsMap(allExports, path.join(CONFIG.outputDir, 'react-exports-map.json'));
    generateModuleMap(allSymbols, path.join(CONFIG.outputDir, 'react-module-map.csv'));
    generateErrorLog(allErrors, path.join(CONFIG.outputDir, 'parse-errors.jsonl'));
    
    console.log('\n✨ Done!');
}

main().catch(console.error);
