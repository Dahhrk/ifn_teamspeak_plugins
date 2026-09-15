#!/usr/bin/env node
/**
 * Conventional commits lint: fail if any commit on this branch diverges
 * from Conventional Commits format.
 *
 * Format: type(scope): description
 *   or:   type: description
 *
 * Allowed types: feat, fix, chore, docs, style, refactor, perf, test,
 *                build, ci, revert, encode
 *
 * Compares HEAD against merge-base with origin/main.
 */
import { execSync } from "node:child_process";

const ALLOWED_TYPES = new Set([
  "feat", "fix", "chore", "docs", "style", "refactor",
  "perf", "test", "build", "ci", "revert", "encode",
]);

const CONVENTIONAL_RE = /^(\w+)(\([^)]+\))?!?:\s.+/;

try {
  execSync("git fetch origin main --quiet 2>/dev/null", { encoding: "utf8" });
} catch { /* shallow clone */ }

let base;
try {
  base = execSync("git merge-base origin/main HEAD", { encoding: "utf8" }).trim();
} catch {
  console.log("commit-lint: could not find merge-base, skipping");
  process.exit(0);
}

const log = execSync(`git log --format="%H %s" ${base}..HEAD`, { encoding: "utf8" }).trim();
if (!log) {
  console.log("commit-lint: no commits to check");
  process.exit(0);
}

const violations = [];
for (const line of log.split("\n")) {
  const sha = line.slice(0, 40);
  const subject = line.slice(41);

  if (subject.startsWith("Merge ") || subject.startsWith("Revert \"")) continue;

  const match = CONVENTIONAL_RE.exec(subject);
  if (!match) {
    violations.push(`${sha.slice(0, 8)}: "${subject}" — not conventional format (type: description)`);
    continue;
  }
  const type = match[1];
  if (!ALLOWED_TYPES.has(type)) {
    violations.push(`${sha.slice(0, 8)}: "${subject}" — unknown type "${type}"`);
  }
}

if (violations.length) {
  console.error("commit-lint failed:\n" + violations.map((v) => `  ${v}`).join("\n"));
  console.error(`\nAllowed types: ${[...ALLOWED_TYPES].join(", ")}`);
  console.error("Format: type(scope): description");
  process.exit(1);
}

const count = log.split("\n").length;
console.log(`commit-lint: ok (${count} commits checked)`);
