#!/usr/bin/env node
/*
 * check-parity.js
 *
 * 当前阶段职责（Phase A/B）：
 * - 字段 parity（先聚焦 Fiber）：ReactJS Flow type 字段 vs C++ struct 字段
 * - 输出缺失字段列表，作为“模块字段 1:1”硬约束的机器检查
 */

'use strict';

const fs = require('fs');
const path = require('path');

const parser = require('@babel/parser');
const traverse = require('@babel/traverse').default;

function parseArgs(argv) {
  const args = {
    jsFiberTypesFile: 'reactjs/packages/react-reconciler/src/ReactInternalTypes.js',
    jsHooksTypesFile: 'reactjs/packages/react-reconciler/src/ReactFiberHooks.js',
    cppFiberStructFile: 'packages/React/test/reconciler/FiberNode.h',
    cppFiberRootStructFile: 'packages/React/test/reconciler/FiberRootNode.h',
    cppInternalTypesStructsFile:
      'packages/React/test/reconciler/ReactInternalTypesNodes.h',
  };

  for (let i = 2; i < argv.length; i++) {
    const token = argv[i];
    if (token === '--js-fiber-types') {
      args.jsFiberTypesFile = argv[++i];
    } else if (token === '--js-hooks-types') {
      args.jsHooksTypesFile = argv[++i];
    } else if (token === '--cpp-fiber-struct') {
      args.cppFiberStructFile = argv[++i];
    } else if (token === '--cpp-fiber-root-struct') {
      args.cppFiberRootStructFile = argv[++i];
    } else if (token === '--cpp-internal-types-structs') {
      args.cppInternalTypesStructsFile = argv[++i];
    } else if (token === '--help' || token === '-h') {
      args.help = true;
    }
  }

  return args;
}

function readTextOrThrow(relPath) {
  const absPath = path.resolve(process.cwd(), relPath);
  if (!fs.existsSync(absPath)) {
    throw new Error(`File not found: ${relPath}`);
  }
  return fs.readFileSync(absPath, 'utf8');
}

function extractFlowObjectTypeFields(jsSourceText, typeName) {
  const ast = parser.parse(jsSourceText, {
    sourceType: 'module',
    plugins: [["flow", {"all": true}], 'jsx', 'classProperties', 'objectRestSpread'],
    errorRecovery: true,
  });

  let fields = null;

  traverse(ast, {
    TypeAlias(typePath) {
      const node = typePath.node;
      if (!node.id || node.id.name !== typeName) return;
      if (!node.right || node.right.type !== 'ObjectTypeAnnotation') return;

      const props = node.right.properties || [];
      const names = [];
      for (const p of props) {
        if (p.type !== 'ObjectTypeProperty') continue;
        if (!p.key) continue;

        if (p.key.type === 'Identifier') {
          names.push(p.key.name);
        } else if (p.key.type === 'StringLiteral') {
          names.push(p.key.value);
        }
      }

      fields = names;
      typePath.stop();
    },
  });

  if (!fields) {
    throw new Error(`Flow type not found or not an object type: ${typeName}`);
  }

  // de-dup + stable order
  return Array.from(new Set(fields));
}

function extractFlowExpandedObjectTypeFields(jsSourceText, typeName) {
  const ast = parser.parse(jsSourceText, {
    sourceType: 'module',
    plugins: [["flow", {all: true}], 'jsx', 'classProperties', 'objectRestSpread'],
    errorRecovery: true,
  });

  const typeAliases = new Map();

  traverse(ast, {
    TypeAlias(typePath) {
      const node = typePath.node;
      if (node.id && node.id.name) {
        typeAliases.set(node.id.name, node.right);
      }
    },
  });

  const visiting = new Set();

  function fieldsFromTypeAnnotation(node) {
    if (!node) return [];

    if (node.type === 'ObjectTypeAnnotation') {
      const names = [];
      const props = node.properties || [];
      for (const p of props) {
        if (p.type === 'ObjectTypeProperty') {
          if (!p.key) continue;
          if (p.key.type === 'Identifier') names.push(p.key.name);
          else if (p.key.type === 'StringLiteral') names.push(p.key.value);
        } else if (p.type === 'ObjectTypeSpreadProperty') {
          names.push(...fieldsFromTypeAnnotation(p.argument));
        }
      }
      return names;
    }

    if (node.type === 'IntersectionTypeAnnotation') {
      const out = [];
      for (const t of node.types || []) {
        out.push(...fieldsFromTypeAnnotation(t));
      }
      return out;
    }

    if (node.type === 'GenericTypeAnnotation') {
      if (node.id && node.id.type === 'Identifier') {
        const name = node.id.name;
        if (visiting.has(name)) return [];
        const aliased = typeAliases.get(name);
        if (!aliased) return [];
        visiting.add(name);
        try {
          return fieldsFromTypeAnnotation(aliased);
        } finally {
          visiting.delete(name);
        }
      }
    }

    return [];
  }

  const root = typeAliases.get(typeName);
  if (!root) {
    throw new Error(`Flow type not found: ${typeName}`);
  }

  const fields = fieldsFromTypeAnnotation(root);
  return Array.from(new Set(fields));
}

function extractFlowNestedObjectTypeFields(jsSourceText, typeName, propName) {
  const ast = parser.parse(jsSourceText, {
    sourceType: 'module',
    plugins: [["flow", {all: true}], 'jsx', 'classProperties', 'objectRestSpread'],
    errorRecovery: true,
  });

  let fields = null;

  traverse(ast, {
    TypeAlias(typePath) {
      const node = typePath.node;
      if (!node.id || node.id.name !== typeName) return;
      if (!node.right || node.right.type !== 'ObjectTypeAnnotation') return;

      const props = node.right.properties || [];
      for (const p of props) {
        if (p.type !== 'ObjectTypeProperty') continue;
        if (!p.key) continue;
        const keyName =
          p.key.type === 'Identifier'
            ? p.key.name
            : p.key.type === 'StringLiteral'
              ? p.key.value
              : null;
        if (keyName !== propName) continue;

        // Flow object property types live in `value`
        const value = p.value;
        if (!value || value.type !== 'ObjectTypeAnnotation') {
          throw new Error(
            `Nested property is not an object type: ${typeName}.${propName}`,
          );
        }

        const nestedProps = value.properties || [];
        const names = [];
        for (const np of nestedProps) {
          if (np.type !== 'ObjectTypeProperty') continue;
          if (!np.key) continue;
          if (np.key.type === 'Identifier') names.push(np.key.name);
          else if (np.key.type === 'StringLiteral') names.push(np.key.value);
        }

        fields = Array.from(new Set(names));
        typePath.stop();
        return;
      }
    },
  });

  if (!fields) {
    throw new Error(`Nested Flow object type fields not found: ${typeName}.${propName}`);
  }

  return fields;
}

function extractCppStructFields(cppHeaderText, structName) {
  const startRe = new RegExp(`\\bstruct\\s+${structName}\\b`);
  const lines = cppHeaderText.split(/\r?\n/);

  let inStruct = false;
  let braceDepth = 0;
  const fields = [];

  for (const line of lines) {
    if (!inStruct) {
      if (startRe.test(line)) {
        inStruct = true;
        // struct line may contain '{'
        if (line.includes('{')) braceDepth++;
      }
      continue;
    }

    if (inStruct) {
      // Track braces to stop at end of struct
      const opens = (line.match(/{/g) || []).length;
      const closes = (line.match(/}/g) || []).length;
      braceDepth += opens - closes;
      if (braceDepth <= 0) break;

      const trimmed = line.trim();
      if (!trimmed) continue;
      if (trimmed.startsWith('//')) continue;
      if (trimmed.startsWith('/*')) continue;
      if (trimmed.includes('(')) continue; // likely method/ctor
      if (!trimmed.endsWith(';')) continue;

      // Very lightweight field parse: take last token before ';'
      const noSemicolon = trimmed.slice(0, -1);
      const tokens = noSemicolon.split(/\s+/);
      if (!tokens.length) continue;

      const nameToken = tokens[tokens.length - 1];
      // Drop pointers/references and templates suffixes
      const name = nameToken.replace(/[*&]/g, '').replace(/\[.*\]$/, '');
      if (/^[A-Za-z_][A-Za-z0-9_]*$/.test(name)) {
        fields.push(name);
      }
    }
  }

  return Array.from(new Set(fields));
}

function getFiberFieldRenameMap() {
  // ReactJS Flow field names that cannot be used verbatim in C++.
  // `return` is a reserved keyword in C++ so the struct uses `returnFiber`.
  return {
    return: 'returnFiber',
  };
}

function main() {
  const args = parseArgs(process.argv);

  if (args.help) {
    process.stdout.write(
      [
        'Usage: node scripts/check-parity.js',
        '',
        'Options:',
        '  --js-fiber-types <path>    (default: reactjs/packages/react-reconciler/src/ReactInternalTypes.js)',
        '  --js-hooks-types <path>    (default: reactjs/packages/react-reconciler/src/ReactFiberHooks.js)',
        '  --cpp-fiber-struct <path>  (default: packages/React/test/reconciler/FiberNode.h)',
        '  --cpp-fiber-root-struct <path> (default: packages/React/test/reconciler/FiberRootNode.h)',
        '  --cpp-internal-types-structs <path> (default: packages/React/test/reconciler/ReactInternalTypesNodes.h)',
        '',
      ].join('\n'),
    );
    process.exit(0);
  }

  const jsText = readTextOrThrow(args.jsFiberTypesFile);
  const jsHooksText = readTextOrThrow(args.jsHooksTypesFile);
  const cppFiberText = readTextOrThrow(args.cppFiberStructFile);
  const cppFiberRootText = readTextOrThrow(args.cppFiberRootStructFile);
  const cppInternalTypesText = readTextOrThrow(args.cppInternalTypesStructsFile);

  const jsFiberFields = extractFlowObjectTypeFields(jsText, 'Fiber');
  const cppFiberFields = extractCppStructFields(cppFiberText, 'FiberNode');

  const cppFieldSet = new Set(cppFiberFields);
  const renameMap = getFiberFieldRenameMap();
  const missingInCpp = jsFiberFields.filter((f) => {
    const cppName = renameMap[f] || f;
    return !cppFieldSet.has(cppName);
  });

  const jsFiberRootFields = extractFlowExpandedObjectTypeFields(jsText, 'FiberRoot');
  const cppFiberRootFields = extractCppStructFields(cppFiberRootText, 'FiberRootNode');

  const cppRootFieldSet = new Set(cppFiberRootFields);
  const missingRootInCpp = jsFiberRootFields.filter((f) => !cppRootFieldSet.has(f));

  const jsDependenciesFields = extractFlowExpandedObjectTypeFields(
    jsText,
    'Dependencies',
  );
  const cppDependenciesFields = extractCppStructFields(
    cppInternalTypesText,
    'DependenciesNode',
  );
  const cppDependenciesFieldSet = new Set(cppDependenciesFields);
  const missingDependenciesInCpp = jsDependenciesFields.filter(
    (f) => !cppDependenciesFieldSet.has(f),
  );

  const jsContextDependencyFields = extractFlowExpandedObjectTypeFields(
    jsText,
    'ContextDependency',
  );
  const cppContextDependencyFields = extractCppStructFields(
    cppInternalTypesText,
    'ContextDependencyNode',
  );
  const cppContextDependencyFieldSet = new Set(cppContextDependencyFields);
  const missingContextDependencyInCpp = jsContextDependencyFields.filter(
    (f) => !cppContextDependencyFieldSet.has(f),
  );

  const jsMemoCacheFields = extractFlowExpandedObjectTypeFields(jsText, 'MemoCache');
  const cppMemoCacheFields = extractCppStructFields(
    cppInternalTypesText,
    'MemoCacheNode',
  );
  const cppMemoCacheFieldSet = new Set(cppMemoCacheFields);
  const missingMemoCacheInCpp = jsMemoCacheFields.filter(
    (f) => !cppMemoCacheFieldSet.has(f),
  );

  const jsUpdateFields = extractFlowObjectTypeFields(jsHooksText, 'Update');
  const cppUpdateFields = extractCppStructFields(cppInternalTypesText, 'UpdateNode');
  const cppUpdateFieldSet = new Set(cppUpdateFields);
  const missingUpdateInCpp = jsUpdateFields.filter((f) => !cppUpdateFieldSet.has(f));

  const jsUpdateQueueFields = extractFlowObjectTypeFields(jsHooksText, 'UpdateQueue');
  const cppUpdateQueueFields = extractCppStructFields(
    cppInternalTypesText,
    'UpdateQueueNode',
  );
  const cppUpdateQueueFieldSet = new Set(cppUpdateQueueFields);
  const missingUpdateQueueInCpp = jsUpdateQueueFields.filter(
    (f) => !cppUpdateQueueFieldSet.has(f),
  );

  const jsHookFields = extractFlowObjectTypeFields(jsHooksText, 'Hook');
  const cppHookFields = extractCppStructFields(cppInternalTypesText, 'HookNode');
  const cppHookFieldSet = new Set(cppHookFields);
  const missingHookInCpp = jsHookFields.filter((f) => !cppHookFieldSet.has(f));

  const jsEffectInstanceFields = extractFlowObjectTypeFields(
    jsHooksText,
    'EffectInstance',
  );
  const cppEffectInstanceFields = extractCppStructFields(
    cppInternalTypesText,
    'EffectInstanceNode',
  );
  const cppEffectInstanceFieldSet = new Set(cppEffectInstanceFields);
  const missingEffectInstanceInCpp = jsEffectInstanceFields.filter(
    (f) => !cppEffectInstanceFieldSet.has(f),
  );

  const jsEffectFields = extractFlowObjectTypeFields(jsHooksText, 'Effect');
  const cppEffectFields = extractCppStructFields(cppInternalTypesText, 'EffectNode');
  const cppEffectFieldSet = new Set(cppEffectFields);
  const missingEffectInCpp = jsEffectFields.filter((f) => !cppEffectFieldSet.has(f));

  const jsStoreInstanceFields = extractFlowObjectTypeFields(
    jsHooksText,
    'StoreInstance',
  );
  const cppStoreInstanceFields = extractCppStructFields(
    cppInternalTypesText,
    'StoreInstanceNode',
  );
  const cppStoreInstanceFieldSet = new Set(cppStoreInstanceFields);
  const missingStoreInstanceInCpp = jsStoreInstanceFields.filter(
    (f) => !cppStoreInstanceFieldSet.has(f),
  );

  const jsStoreConsistencyCheckFields = extractFlowObjectTypeFields(
    jsHooksText,
    'StoreConsistencyCheck',
  );
  const cppStoreConsistencyCheckFields = extractCppStructFields(
    cppInternalTypesText,
    'StoreConsistencyCheckNode',
  );
  const cppStoreConsistencyCheckFieldSet = new Set(cppStoreConsistencyCheckFields);
  const missingStoreConsistencyCheckInCpp = jsStoreConsistencyCheckFields.filter(
    (f) => !cppStoreConsistencyCheckFieldSet.has(f),
  );

  const jsFunctionComponentUpdateQueueFields = extractFlowObjectTypeFields(
    jsHooksText,
    'FunctionComponentUpdateQueue',
  );
  const cppFunctionComponentUpdateQueueFields = extractCppStructFields(
    cppInternalTypesText,
    'FunctionComponentUpdateQueueNode',
  );
  const cppFunctionComponentUpdateQueueFieldSet = new Set(
    cppFunctionComponentUpdateQueueFields,
  );
  const missingFunctionComponentUpdateQueueInCpp =
    jsFunctionComponentUpdateQueueFields.filter(
      (f) => !cppFunctionComponentUpdateQueueFieldSet.has(f),
    );

  const jsEventFunctionPayloadFields = extractFlowObjectTypeFields(
    jsHooksText,
    'EventFunctionPayload',
  );
  const cppEventFunctionPayloadFields = extractCppStructFields(
    cppInternalTypesText,
    'EventFunctionPayloadNode',
  );
  const cppEventFunctionPayloadFieldSet = new Set(cppEventFunctionPayloadFields);
  const missingEventFunctionPayloadInCpp = jsEventFunctionPayloadFields.filter(
    (f) => !cppEventFunctionPayloadFieldSet.has(f),
  );

  const jsEventFunctionPayloadRefFields = extractFlowNestedObjectTypeFields(
    jsHooksText,
    'EventFunctionPayload',
    'ref',
  );
  const cppEventFunctionPayloadRefFields = extractCppStructFields(
    cppInternalTypesText,
    'EventFunctionPayloadRefNode',
  );
  const cppEventFunctionPayloadRefFieldSet = new Set(
    cppEventFunctionPayloadRefFields,
  );
  const missingEventFunctionPayloadRefInCpp =
    jsEventFunctionPayloadRefFields.filter(
      (f) => !cppEventFunctionPayloadRefFieldSet.has(f),
    );

  const report = {
    checkedAt: new Date().toISOString(),
    targets: {
      jsFiberTypesFile: args.jsFiberTypesFile,
      jsHooksTypesFile: args.jsHooksTypesFile,
      cppFiberStructFile: args.cppFiberStructFile,
      cppFiberRootStructFile: args.cppFiberRootStructFile,
      cppInternalTypesStructsFile: args.cppInternalTypesStructsFile,
    },
    Fiber: {
      counts: {
        js_fields: jsFiberFields.length,
        cpp_fields: cppFiberFields.length,
        missing_in_cpp: missingInCpp.length,
      },
      missing_in_cpp: missingInCpp,
    },
    FiberRoot: {
      counts: {
        js_fields: jsFiberRootFields.length,
        cpp_fields: cppFiberRootFields.length,
        missing_in_cpp: missingRootInCpp.length,
      },
      missing_in_cpp: missingRootInCpp,
    },
    Dependencies: {
      counts: {
        js_fields: jsDependenciesFields.length,
        cpp_fields: cppDependenciesFields.length,
        missing_in_cpp: missingDependenciesInCpp.length,
      },
      missing_in_cpp: missingDependenciesInCpp,
    },
    ContextDependency: {
      counts: {
        js_fields: jsContextDependencyFields.length,
        cpp_fields: cppContextDependencyFields.length,
        missing_in_cpp: missingContextDependencyInCpp.length,
      },
      missing_in_cpp: missingContextDependencyInCpp,
    },
    MemoCache: {
      counts: {
        js_fields: jsMemoCacheFields.length,
        cpp_fields: cppMemoCacheFields.length,
        missing_in_cpp: missingMemoCacheInCpp.length,
      },
      missing_in_cpp: missingMemoCacheInCpp,
    },
    Update: {
      counts: {
        js_fields: jsUpdateFields.length,
        cpp_fields: cppUpdateFields.length,
        missing_in_cpp: missingUpdateInCpp.length,
      },
      missing_in_cpp: missingUpdateInCpp,
    },
    UpdateQueue: {
      counts: {
        js_fields: jsUpdateQueueFields.length,
        cpp_fields: cppUpdateQueueFields.length,
        missing_in_cpp: missingUpdateQueueInCpp.length,
      },
      missing_in_cpp: missingUpdateQueueInCpp,
    },
    Hook: {
      counts: {
        js_fields: jsHookFields.length,
        cpp_fields: cppHookFields.length,
        missing_in_cpp: missingHookInCpp.length,
      },
      missing_in_cpp: missingHookInCpp,
    },
    EffectInstance: {
      counts: {
        js_fields: jsEffectInstanceFields.length,
        cpp_fields: cppEffectInstanceFields.length,
        missing_in_cpp: missingEffectInstanceInCpp.length,
      },
      missing_in_cpp: missingEffectInstanceInCpp,
    },
    Effect: {
      counts: {
        js_fields: jsEffectFields.length,
        cpp_fields: cppEffectFields.length,
        missing_in_cpp: missingEffectInCpp.length,
      },
      missing_in_cpp: missingEffectInCpp,
    },
    StoreInstance: {
      counts: {
        js_fields: jsStoreInstanceFields.length,
        cpp_fields: cppStoreInstanceFields.length,
        missing_in_cpp: missingStoreInstanceInCpp.length,
      },
      missing_in_cpp: missingStoreInstanceInCpp,
    },
    StoreConsistencyCheck: {
      counts: {
        js_fields: jsStoreConsistencyCheckFields.length,
        cpp_fields: cppStoreConsistencyCheckFields.length,
        missing_in_cpp: missingStoreConsistencyCheckInCpp.length,
      },
      missing_in_cpp: missingStoreConsistencyCheckInCpp,
    },
    FunctionComponentUpdateQueue: {
      counts: {
        js_fields: jsFunctionComponentUpdateQueueFields.length,
        cpp_fields: cppFunctionComponentUpdateQueueFields.length,
        missing_in_cpp: missingFunctionComponentUpdateQueueInCpp.length,
      },
      missing_in_cpp: missingFunctionComponentUpdateQueueInCpp,
    },
    EventFunctionPayload: {
      counts: {
        js_fields: jsEventFunctionPayloadFields.length,
        cpp_fields: cppEventFunctionPayloadFields.length,
        missing_in_cpp: missingEventFunctionPayloadInCpp.length,
      },
      missing_in_cpp: missingEventFunctionPayloadInCpp,
    },
    EventFunctionPayloadRef: {
      counts: {
        js_fields: jsEventFunctionPayloadRefFields.length,
        cpp_fields: cppEventFunctionPayloadRefFields.length,
        missing_in_cpp: missingEventFunctionPayloadRefInCpp.length,
      },
      missing_in_cpp: missingEventFunctionPayloadRefInCpp,
    },
  };

  process.stdout.write(JSON.stringify(report, null, 2) + '\n');

  if (
    missingInCpp.length > 0 ||
    missingRootInCpp.length > 0 ||
    missingDependenciesInCpp.length > 0 ||
    missingContextDependencyInCpp.length > 0 ||
    missingMemoCacheInCpp.length > 0 ||
    missingUpdateInCpp.length > 0 ||
    missingUpdateQueueInCpp.length > 0 ||
    missingHookInCpp.length > 0 ||
    missingEffectInstanceInCpp.length > 0 ||
    missingEffectInCpp.length > 0 ||
    missingStoreInstanceInCpp.length > 0 ||
    missingStoreConsistencyCheckInCpp.length > 0 ||
    missingFunctionComponentUpdateQueueInCpp.length > 0
    ||
    missingEventFunctionPayloadInCpp.length > 0 ||
    missingEventFunctionPayloadRefInCpp.length > 0
  ) {
    process.exitCode = 2;
  }
}

main();
