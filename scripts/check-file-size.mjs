#!/usr/bin/env node
/**
 * File size limit: fail if any src/ file exceeds MAX_LINES.
 * Enforces Dune rule #4 — new work in isolated files, not sprawling god files.
 *
 * Default: 400 lines. Override with FILE_MAX_LINES env var.
 * Escape hatch: add to ALLOWLIST with architecture ticket.
 */
import { readdirSync, readFileSync, statSync, existsSync } from "node:fs";
import path from "node:path";

const ROOT = process.cwd();
const SRC = path.join(ROOT, "src");
const MAX_LINES = Number(process.env.FILE_MAX_LINES || 400);

const ALLOWLIST = [
  // { path: "src/features/legacy/BigFile.tsx", ticket: "ARCH-100" },
];

const violations = [];

function walk(dir, files = []) {
  if (!existsSync(dir)) return files;
  for (const name of readdirSync(dir)) {
    const p = path.join(dir, name);
    if (statSync(p).isDirectory()) {
      if (name === "node_modules" || name === "dist") continue;
      walk(p, files);
    } else if (/\.(ts|tsx|js|jsx|css)$/.test(name)) {
      files.push(p);
    }
  }
  return files;
}

function isAllowlisted(rel) {
  return ALLOWLIST.some((e) => e.path === rel);
}

for (const file of walk(SRC)) {
  const rel = path.relative(ROOT, file).replace(/\\/g, "/");
  if (isAllowlisted(rel)) continue;
  const lines = readFileSync(file, "utf8").split(/\r?\n/).length;
  if (lines > MAX_LINES) {
    violations.push(`${rel}: ${lines} lines (max ${MAX_LINES})`);
  }
}

if (violations.length) {
  console.error(`file-size-limit failed (max ${MAX_LINES} lines):\n` + violations.map((v) => `  ${v}`).join("\n"));
  console.error("\nSplit large files into isolated feature modules (Dune rule #4).");
  process.exit(1);
}

console.log(`file-size-limit: ok (${walk(SRC).length} files, max ${MAX_LINES} lines)`);
