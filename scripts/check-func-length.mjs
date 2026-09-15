#!/usr/bin/env node
/**
 * Max function length: fail if any function in src/ exceeds MAX_FUNC_LINES.
 * Forces decomposition — agents write 200-line functions without thinking.
 *
 * Default: 60 lines. Override with FUNC_MAX_LINES env var.
 * Counts from opening brace to closing brace.
 * Escape hatch: ALLOWLIST with architecture ticket.
 */
import { readdirSync, readFileSync, statSync, existsSync } from "node:fs";
import path from "node:path";
import * as ts from "typescript";

const ROOT = process.cwd();
const SRC = path.join(ROOT, "src");
const MAX_LINES = Number(process.env.FUNC_MAX_LINES || 60);

const ALLOWLIST = [
  // { path: "src/features/legacy/bigReducer.ts", func: "reduce", ticket: "ARCH-200" },
];

const violations = [];

function walk(dir, files = []) {
  if (!existsSync(dir)) return files;
  for (const name of readdirSync(dir)) {
    const p = path.join(dir, name);
    if (statSync(p).isDirectory()) {
      if (name === "node_modules" || name === "dist") continue;
      walk(p, files);
    } else if (/\.tsx?$/.test(name)) {
      files.push(p);
    }
  }
  return files;
}

function lineOf(text, pos) {
  return text.slice(0, pos).split(/\r?\n/).length;
}

function isAllowlisted(rel, funcName) {
  return ALLOWLIST.some((e) => e.path === rel && e.func === funcName);
}

function checkFile(abs) {
  const rel = path.relative(ROOT, abs).replace(/\\/g, "/");
  const text = readFileSync(abs, "utf8");
  const kind = rel.endsWith(".tsx") ? ts.ScriptKind.TSX : ts.ScriptKind.TS;
  const sf = ts.createSourceFile(rel, text, ts.ScriptTarget.Latest, true, kind);

  function visit(node) {
    let name = "<anonymous>";
    let body = null;

    if (ts.isFunctionDeclaration(node) && node.body) {
      name = node.name?.text || "<anonymous>";
      body = node.body;
    } else if (ts.isArrowFunction(node) && ts.isBlock(node.body)) {
      const parent = node.parent;
      if (ts.isVariableDeclaration(parent) && ts.isIdentifier(parent.name)) {
        name = parent.name.text;
      }
      body = node.body;
    } else if (ts.isMethodDeclaration(node) && node.body) {
      name = node.name?.getText(sf) || "<method>";
      body = node.body;
    }

    if (body) {
      const startLine = lineOf(text, body.getStart(sf));
      const endLine = lineOf(text, body.getEnd());
      const length = endLine - startLine + 1;
      if (length > MAX_LINES && !isAllowlisted(rel, name)) {
        violations.push(`${rel}:${startLine}: ${name}() is ${length} lines (max ${MAX_LINES})`);
      }
    }

    ts.forEachChild(node, visit);
  }

  visit(sf);
}

for (const file of walk(SRC)) {
  checkFile(file);
}

if (violations.length) {
  console.error(`func-length failed (max ${MAX_LINES} lines):\n` + violations.map((v) => `  ${v}`).join("\n"));
  console.error("\nBreak large functions into smaller, named pieces.");
  process.exit(1);
}

console.log(`func-length: ok (max ${MAX_LINES} lines per function)`);
