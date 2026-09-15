#!/usr/bin/env node
/**
 * Duplicate code detection: find copy-pasted blocks across src/ files.
 *
 * Uses a simple token-based approach: normalize each file's lines,
 * hash sliding windows, and flag when the same hash appears in
 * different files.
 *
 * Threshold: MIN_LINES consecutive matching lines (default 10).
 */
import { readdirSync, readFileSync, statSync, existsSync } from "node:fs";
import { createHash } from "node:crypto";
import path from "node:path";

const ROOT = process.cwd();
const SRC = path.join(ROOT, "src");
const MIN_LINES = Number(process.env.DUP_MIN_LINES || 10);

function walk(dir, files = []) {
  if (!existsSync(dir)) return files;
  for (const name of readdirSync(dir)) {
    const p = path.join(dir, name);
    if (statSync(p).isDirectory()) {
      if (name === "node_modules" || name === "dist") continue;
      walk(p, files);
    } else if (/\.(ts|tsx)$/.test(name)) {
      files.push(p);
    }
  }
  return files;
}

function normalize(line) {
  return line.replace(/\s+/g, " ").trim();
}

function hashBlock(lines) {
  return createHash("md5").update(lines.join("\n")).digest("hex");
}

const files = walk(SRC);
const blockMap = new Map();
const duplicates = [];

for (const file of files) {
  const rel = path.relative(ROOT, file).replace(/\\/g, "/");
  const rawLines = readFileSync(file, "utf8").split(/\r?\n/).map(normalize);
  const lines = [];
  const lineMap = [];
  for (let j = 0; j < rawLines.length; j++) {
    if (rawLines[j].length > 0) {
      lines.push(rawLines[j]);
      lineMap.push(j + 1);
    }
  }

  if (lines.length < MIN_LINES) continue;

  for (let i = 0; i <= lines.length - MIN_LINES; i++) {
    const block = lines.slice(i, i + MIN_LINES);
    const hash = hashBlock(block);

    if (blockMap.has(hash)) {
      const existing = blockMap.get(hash);
      if (existing.file !== rel) {
        const key = [existing.file, rel].sort().join(" <> ");
        if (!duplicates.find((d) => d.key === key && d.hash === hash)) {
          duplicates.push({
            key,
            hash,
            fileA: existing.file,
            lineA: existing.line,
            fileB: rel,
            lineB: lineMap[i],
            preview: block[0].slice(0, 80),
          });
        }
      }
    } else {
      blockMap.set(hash, { file: rel, line: lineMap[i] });
    }
  }
}

if (duplicates.length) {
  console.warn(`duplicate-code: ${duplicates.length} duplicated blocks (${MIN_LINES}+ lines) found:`);
  for (const d of duplicates.slice(0, 20)) {
    console.warn(`  ${d.fileA}:${d.lineA} <> ${d.fileB}:${d.lineB} — "${d.preview}..."`);
  }
  if (duplicates.length > 20) {
    console.warn(`  ... and ${duplicates.length - 20} more`);
  }
  console.warn("\nExtract shared code into a common module.");
  process.exit(1);
}

console.log(`duplicate-code: ok (${files.length} files scanned, min block ${MIN_LINES} lines)`);
