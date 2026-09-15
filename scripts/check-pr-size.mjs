#!/usr/bin/env node
/**
 * PR size check: warn or fail if the diff is too large.
 * Enforces "one verifiable unit per PR."
 *
 * Thresholds:
 *   WARN_FILES   warn above this many changed files (default 15)
 *   MAX_FILES    fail above this (default 30)
 *   WARN_LINES   warn above this many added+deleted lines (default 500)
 *   MAX_LINES    fail above this (default 1500)
 *
 * Compares HEAD against the merge-base with origin/main.
 */
import { execSync } from "node:child_process";

const WARN_FILES = Number(process.env.PR_WARN_FILES || 15);
const MAX_FILES = Number(process.env.PR_MAX_FILES || 30);
const WARN_LINES = Number(process.env.PR_WARN_LINES || 500);
const MAX_LINES = Number(process.env.PR_MAX_LINES || 1500);

try {
  execSync("git fetch origin main --quiet 2>/dev/null", { encoding: "utf8" });
} catch {
  // may fail in shallow clones
}

let base;
try {
  base = execSync("git merge-base origin/main HEAD", { encoding: "utf8" }).trim();
} catch {
  console.log("pr-size: could not find merge-base with origin/main, skipping");
  process.exit(0);
}

const diffStat = execSync(`git diff --stat ${base} HEAD`, { encoding: "utf8" });
const shortstat = execSync(`git diff --shortstat ${base} HEAD`, { encoding: "utf8" }).trim();

const filesMatch = shortstat.match(/(\d+) files? changed/);
const insertMatch = shortstat.match(/(\d+) insertions?/);
const deleteMatch = shortstat.match(/(\d+) deletions?/);

const changedFiles = filesMatch ? Number(filesMatch[1]) : 0;
const insertions = insertMatch ? Number(insertMatch[1]) : 0;
const deletions = deleteMatch ? Number(deleteMatch[1]) : 0;
const totalLines = insertions + deletions;

console.log(`pr-size: ${changedFiles} files changed, +${insertions} -${deletions} (${totalLines} total lines)`);

let failed = false;

if (changedFiles > MAX_FILES) {
  console.error(`pr-size FAIL: ${changedFiles} files exceeds max ${MAX_FILES}. Split into smaller PRs.`);
  failed = true;
} else if (changedFiles > WARN_FILES) {
  console.warn(`pr-size WARN: ${changedFiles} files is above ${WARN_FILES}. Consider splitting.`);
}

if (totalLines > MAX_LINES) {
  console.error(`pr-size FAIL: ${totalLines} lines exceeds max ${MAX_LINES}. One verifiable unit per PR.`);
  failed = true;
} else if (totalLines > WARN_LINES) {
  console.warn(`pr-size WARN: ${totalLines} lines is above ${WARN_LINES}. Consider splitting.`);
}

if (failed) process.exit(1);
console.log("pr-size: ok");
