#!/usr/bin/env node
/*
 * check-line-parity.js
 *
 * Phase C (logic line parity):
 * - Enforce 1:1 line parity between a JS source function and its C++ translation.
 * - This is a mechanical gate; semantic parity still requires runtime tests.
 */

'use strict';

const fs = require('fs');
const path = require('path');

const parser = require('@babel/parser');
const traverse = require('@babel/traverse').default;

function readTextOrThrow(relPath) {
  const absPath = path.resolve(process.cwd(), relPath);
  if (!fs.existsSync(absPath)) {
    throw new Error(`File not found: ${relPath}`);
  }
  return fs.readFileSync(absPath, 'utf8');
}

function parseArgs(argv) {
  const args = {
    configFile: 'docs/reactcpp/line-parity-targets.json',
  };

  for (let i = 2; i < argv.length; i++) {
    const token = argv[i];
    if (token === '--config') {
      args.configFile = argv[++i];
    } else if (token === '--help' || token === '-h') {
      args.help = true;
    }
  }

  return args;
}

function findJsFunctionLineRange(jsSourceText, functionName) {
  const ast = parser.parse(jsSourceText, {
    sourceType: 'module',
    plugins: [["flow", {all: true}], 'jsx', 'classProperties', 'objectRestSpread'],
    errorRecovery: true,
  });

  let range = null;

  traverse(ast, {
    FunctionDeclaration(p) {
      const node = p.node;
      if (!node.id || node.id.name !== functionName) return;
      if (!node.loc) return;
      range = {
        startLine: node.loc.start.line,
        endLine: node.loc.end.line,
      };
      p.stop();
    },
  });

  if (!range) {
    throw new Error(`JS function not found: ${functionName}`);
  }

  return range;
}

function sliceLines(text, startLine, endLine) {
  const lines = text.split(/\r?\n/);
  const startIdx = Math.max(0, startLine - 1);
  const endIdx = Math.min(lines.length - 1, endLine - 1);
  return lines.slice(startIdx, endIdx + 1);
}

function findCppFunctionLines(cppText, cppFunctionName) {
  const lines = cppText.split(/\r?\n/);
  const sigRe = new RegExp(`\\b${cppFunctionName}\\s*\\(`);

  let start = -1;
  for (let i = 0; i < lines.length; i++) {
    if (sigRe.test(lines[i])) {
      start = i;
      break;
    }
  }
  if (start === -1) {
    throw new Error(`C++ function signature not found: ${cppFunctionName}`);
  }

  // Find the first opening brace after the signature line, then match braces.
  let braceDepth = 0;
  let sawOpening = false;
  let end = -1;

  for (let i = start; i < lines.length; i++) {
    const line = lines[i];
    for (const ch of line) {
      if (ch === '{') {
        braceDepth++;
        sawOpening = true;
      } else if (ch === '}') {
        braceDepth--;
        if (sawOpening && braceDepth === 0) {
          end = i;
          break;
        }
      }
    }
    if (end !== -1) break;
  }

  if (end === -1) {
    throw new Error(`C++ function body braces not balanced: ${cppFunctionName}`);
  }

  return lines.slice(start, end + 1);
}

function main() {
  const args = parseArgs(process.argv);
  if (args.help) {
    process.stdout.write(
      [
        'Usage: node scripts/check-line-parity.js',
        '',
        'Options:',
        '  --config <path> (default: docs/reactcpp/line-parity-targets.json)',
        '',
      ].join('\n'),
    );
    process.exit(0);
  }

  const configText = readTextOrThrow(args.configFile);
  const config = JSON.parse(configText);
  const targets = Array.isArray(config.targets) ? config.targets : [];

  if (targets.length === 0) {
    throw new Error('No targets in config.');
  }

  const results = [];
  let hasMismatch = false;

  for (const t of targets) {
    const jsText = readTextOrThrow(t.jsFile);
    const cppText = readTextOrThrow(t.cppFile);

    const jsRange = findJsFunctionLineRange(jsText, t.functionName);
    const jsLines = sliceLines(jsText, jsRange.startLine, jsRange.endLine);

    const cppLines = findCppFunctionLines(cppText, t.cppFunctionName);

    const ok = jsLines.length === cppLines.length;
    if (!ok) hasMismatch = true;

    results.push({
      target: t,
      js: {
        startLine: jsRange.startLine,
        endLine: jsRange.endLine,
        lineCount: jsLines.length,
      },
      cpp: {
        lineCount: cppLines.length,
      },
      ok,
    });
  }

  const report = {
    checkedAt: new Date().toISOString(),
    configFile: args.configFile,
    results,
  };

  process.stdout.write(JSON.stringify(report, null, 2) + '\n');

  if (hasMismatch) {
    process.exitCode = 2;
  }
}

main();
