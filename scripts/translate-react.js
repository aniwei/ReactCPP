#!/usr/bin/env node
/*
 * translate-react.js
 *
 * 当前阶段职责（Phase A）：
 * - 扫描 reactjs/ 源码
 * - 生成“模块/函数对照表（精确到函数）”到 docs/reactcpp/generated/
 *
 * 后续阶段可扩展：
 * - 生成 C++ 骨架文件（.h/.cpp）
 * - 输出更强的 AST parity 描述
 */

'use strict';

const fs = require('fs');
const path = require('path');

const parser = require('@babel/parser');
const traverse = require('@babel/traverse').default;

function parseArgs(argv) {
  const args = {
    jsRoot: 'reactjs',
    outDir: 'docs/reactcpp/generated',
    includePattern: /reactjs\/packages\/.+\/(src|shared)\/.+\.(js|jsx)$/,
  };

  for (let i = 2; i < argv.length; i++) {
    const token = argv[i];
    if (token === '--js-root') {
      args.jsRoot = argv[++i];
    } else if (token === '--out') {
      args.outDir = argv[++i];
    } else if (token === '--help' || token === '-h') {
      args.help = true;
    } else {
      // ignore unknown flags for forward-compat
    }
  }

  return args;
}

function ensureDir(dirPath) {
  fs.mkdirSync(dirPath, {recursive: true});
}

function listFilesRecursive(rootDir) {
  const results = [];
  const stack = [rootDir];

  while (stack.length) {
    const currentDir = stack.pop();
    const entries = fs.readdirSync(currentDir, {withFileTypes: true});

    for (const entry of entries) {
      const fullPath = path.join(currentDir, entry.name);
      if (entry.isDirectory()) {
        // Skip huge/irrelevant dirs
        if (entry.name === 'node_modules' || entry.name === '.git' || entry.name === 'build') {
          continue;
        }
        stack.push(fullPath);
      } else if (entry.isFile()) {
        results.push(fullPath);
      }
    }
  }

  return results;
}

function parseJsModule(filePath, sourceText) {
  // Some ReactJS files use Flow-only syntax that @babel/parser doesn't support
  // (e.g. typed catch binding). For our purposes (module/function index), it's
  // safe to strip these annotations before parsing.
  const preprocessedSourceText = sourceText
    // Flow typed catch binding: `catch (e: mixed)` -> `catch (e)`
    .replace(/\bcatch\s*\(\s*([A-Za-z_$][\w$]*)\s*:\s*[^)]+\)/g, 'catch ($1)');

  // Flow component syntax in devtools type-only files isn't needed for runtime
  // function mapping. Treat as a type-only module with no functions.
  if (/\bexport\s+type\s+\w+\s*=\s*component\s*\(/m.test(preprocessedSourceText)) {
    return {
      exports: {named: [], hasDefault: false},
      functions: [],
    };
  }

  let ast;
  try {
    const commonPlugins = [
      'jsx',
      'classProperties',
      'classPrivateProperties',
      'classPrivateMethods',
      'optionalChaining',
      'nullishCoalescingOperator',
      'objectRestSpread',
      'dynamicImport',
      'exportDefaultFrom',
      'exportNamespaceFrom',
      'topLevelAwait',
      'importAttributes',
    ];

    // Prefer Flow first (React repo majority)
    try {
      ast = parser.parse(preprocessedSourceText, {
        sourceType: 'module',
        plugins: [["flow", {"all": true}], 'flowComments', ...commonPlugins],
        errorRecovery: true,
        ranges: false,
      });
    } catch (flowErr) {
      // Fallback to TS for a small number of .js files using TS-like syntax
      ast = parser.parse(preprocessedSourceText, {
        sourceType: 'module',
        plugins: ['typescript', ...commonPlugins],
        errorRecovery: true,
        ranges: false,
      });
    }
  } catch (err) {
    const message = err && err.message ? err.message : String(err);
    const wrapped = new Error(`[translate-react] parse failed: ${filePath}: ${message}`);
    wrapped.cause = err;
    throw wrapped;
  }

  const functions = [];
  const exports = {
    named: new Set(),
    hasDefault: false,
  };

  function pushFunction(kind, name, loc, isExported) {
    if (!name || !loc) return;
    functions.push({
      kind,
      name,
      loc: {
        start: {line: loc.start.line, column: loc.start.column},
        end: {line: loc.end.line, column: loc.end.column},
      },
      exported: Boolean(isExported),
    });
  }

  traverse(ast, {
    Program(programPath) {
      // Only top-level bindings
      for (const nodePath of programPath.get('body')) {
        const node = nodePath.node;

        if (node.type === 'ExportDefaultDeclaration') {
          exports.hasDefault = true;
        }

        if (node.type === 'ExportNamedDeclaration') {
          if (node.declaration) {
            if (node.declaration.type === 'FunctionDeclaration') {
              const fn = node.declaration;
              exports.named.add(fn.id ? fn.id.name : '');
              pushFunction('function', fn.id ? fn.id.name : null, fn.loc, true);
            } else if (node.declaration.type === 'VariableDeclaration') {
              for (const decl of node.declaration.declarations) {
                if (!decl.id || decl.id.type !== 'Identifier') continue;
                const name = decl.id.name;
                exports.named.add(name);
                if (
                  decl.init &&
                  (decl.init.type === 'FunctionExpression' || decl.init.type === 'ArrowFunctionExpression')
                ) {
                  pushFunction(
                    decl.init.type === 'ArrowFunctionExpression' ? 'arrow' : 'functionExpression',
                    name,
                    decl.init.loc,
                    true,
                  );
                }
              }
            } else if (node.declaration.type === 'ClassDeclaration') {
              const cls = node.declaration;
              if (cls.id && cls.id.name) {
                exports.named.add(cls.id.name);
              }
              // Class methods as functions
              for (const bodyEl of cls.body.body) {
                if (bodyEl.type === 'ClassMethod' || bodyEl.type === 'ClassPrivateMethod') {
                  const methodName =
                    bodyEl.key && bodyEl.key.type === 'Identifier'
                      ? bodyEl.key.name
                      : bodyEl.key && bodyEl.key.type === 'StringLiteral'
                        ? bodyEl.key.value
                        : null;
                  pushFunction('classMethod', `${cls.id ? cls.id.name : 'AnonymousClass'}.${methodName}`, bodyEl.loc, true);
                }
              }
            }
          }

          if (node.specifiers && node.specifiers.length) {
            for (const spec of node.specifiers) {
              if (spec.exported && spec.exported.type === 'Identifier') {
                exports.named.add(spec.exported.name);
              }
            }
          }
        }

        // Non-exported top-level functions
        if (node.type === 'FunctionDeclaration') {
          const name = node.id ? node.id.name : null;
          const isExported = exports.named.has(name);
          pushFunction('function', name, node.loc, isExported);
        }

        if (node.type === 'VariableDeclaration') {
          for (const decl of node.declarations) {
            if (!decl.id || decl.id.type !== 'Identifier') continue;
            const name = decl.id.name;
            if (
              decl.init &&
              (decl.init.type === 'FunctionExpression' || decl.init.type === 'ArrowFunctionExpression')
            ) {
              const isExported = exports.named.has(name);
              pushFunction(
                decl.init.type === 'ArrowFunctionExpression' ? 'arrow' : 'functionExpression',
                name,
                decl.init.loc,
                isExported,
              );
            }
          }
        }

        if (node.type === 'ClassDeclaration') {
          const cls = node;
          const className = cls.id ? cls.id.name : 'AnonymousClass';
          for (const bodyEl of cls.body.body) {
            if (bodyEl.type === 'ClassMethod' || bodyEl.type === 'ClassPrivateMethod') {
              const methodName =
                bodyEl.key && bodyEl.key.type === 'Identifier'
                  ? bodyEl.key.name
                  : bodyEl.key && bodyEl.key.type === 'StringLiteral'
                    ? bodyEl.key.value
                    : null;
              pushFunction('classMethod', `${className}.${methodName}`, bodyEl.loc, false);
            }
          }
        }
      }
    },
  });

  // De-dup by (kind+name+startLine)
  const dedup = new Map();
  for (const fn of functions) {
    const key = `${fn.kind}:${fn.name}:${fn.loc.start.line}`;
    if (!dedup.has(key)) dedup.set(key, fn);
  }

  return {
    exports: {
      named: Array.from(exports.named).filter(Boolean).sort(),
      hasDefault: exports.hasDefault,
    },
    functions: Array.from(dedup.values()).sort((a, b) => {
      if (a.loc.start.line !== b.loc.start.line) return a.loc.start.line - b.loc.start.line;
      return a.name.localeCompare(b.name);
    }),
  };
}

function main() {
  const args = parseArgs(process.argv);

  if (args.help) {
    process.stdout.write(
      [
        'Usage: node scripts/translate-react.js [--js-root reactjs] [--out docs/reactcpp/generated]',
        '',
        'Outputs:',
        '  - reactjs-module-index.json',
        '  - reactjs-function-map.json',
        '',
      ].join('\n'),
    );
    process.exit(0);
  }

  const repoRoot = process.cwd();
  const jsRootAbs = path.resolve(repoRoot, args.jsRoot);
  const outDirAbs = path.resolve(repoRoot, args.outDir);

  if (!fs.existsSync(jsRootAbs)) {
    console.error(`[translate-react] jsRoot not found: ${jsRootAbs}`);
    process.exit(1);
  }

  ensureDir(outDirAbs);

  const allFiles = listFilesRecursive(jsRootAbs);
  const jsFiles = allFiles.filter((p) => args.includePattern.test(p.replace(/\\/g, '/')));

  const modules = [];
  const functions = [];
  const parseErrors = [];

  for (const absPath of jsFiles) {
    const relPath = path.relative(repoRoot, absPath).replace(/\\/g, '/');
    let sourceText;
    try {
      sourceText = fs.readFileSync(absPath, 'utf8');
    } catch (e) {
      continue;
    }

    let parsed;
    try {
      parsed = parseJsModule(absPath, sourceText);
    } catch (e) {
      parseErrors.push({
        file: relPath,
        message: e && e.message ? e.message : String(e),
      });
      continue;
    }

    modules.push({
      module: relPath,
      exports: parsed.exports,
      functionCount: parsed.functions.length,
    });

    for (const fn of parsed.functions) {
      functions.push({
        module: relPath,
        name: fn.name,
        kind: fn.kind,
        exported: fn.exported,
        js_loc: fn.loc,
      });
    }
  }

  modules.sort((a, b) => a.module.localeCompare(b.module));
  functions.sort((a, b) => {
    if (a.module !== b.module) return a.module.localeCompare(b.module);
    if (a.js_loc.start.line !== b.js_loc.start.line) return a.js_loc.start.line - b.js_loc.start.line;
    return a.name.localeCompare(b.name);
  });

  const moduleIndexPath = path.join(outDirAbs, 'reactjs-module-index.json');
  const functionMapPath = path.join(outDirAbs, 'reactjs-function-map.json');

  fs.writeFileSync(
    moduleIndexPath,
    JSON.stringify({generatedAt: new Date().toISOString(), modules, parseErrors}, null, 2),
  );
  fs.writeFileSync(
    functionMapPath,
    JSON.stringify({generatedAt: new Date().toISOString(), functions, parseErrors}, null, 2),
  );

  process.stdout.write(
    `[translate-react] modules=${modules.length} functions=${functions.length} parseErrors=${parseErrors.length}\n` +
      `[translate-react] wrote: ${path.relative(repoRoot, moduleIndexPath)}\n` +
      `[translate-react] wrote: ${path.relative(repoRoot, functionMapPath)}\n`,
  );

  if (parseErrors.length > 0) {
    process.exitCode = 2;
  }
}

main();
