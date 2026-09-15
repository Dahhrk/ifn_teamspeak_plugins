#!/usr/bin/env node
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HEADER = "ts\tsource\tworkspace\tsmell\tn\taction\tevidence";
const SOURCES = new Set(["local", "grok", "cursor-auto", "devin"]);
const ACTIONS = new Set(["note", "encode"]);
const SLUG = /^[a-z0-9-]+$/;
const N_RE = /^[1-9]\d*$/;

function fail(message) {
  process.stderr.write(`${message}\n`);
  process.exit(1);
}

function walkFactory(start) {
  let dir = resolve(start);
  while (true) {
    if (
      existsSync(join(dir, "audit")) &&
      existsSync(join(dir, "docs", "SELF-IMPROVE.md"))
    ) {
      return dir;
    }
    const parent = dirname(dir);
    if (parent === dir) {
      return null;
    }
    dir = parent;
  }
}

function resolveFactoryRoot(flag) {
  if (flag) {
    return resolve(String(flag));
  }
  const here = dirname(fileURLToPath(import.meta.url));
  return walkFactory(here) || walkFactory(process.cwd()) || null;
}

function parseFlags(argv) {
  const flags = Object.create(null);
  const positionals = [];
  for (let i = 0; i < argv.length; i += 1) {
    const token = argv[i];
    if (!token.startsWith("--")) {
      positionals.push(token);
      continue;
    }
    const body = token.slice(2);
    const eq = body.indexOf("=");
    if (eq !== -1) {
      flags[body.slice(0, eq)] = body.slice(eq + 1);
      continue;
    }
    const next = argv[i + 1];
    if (next === undefined || next.startsWith("--")) {
      flags[body] = true;
      continue;
    }
    flags[body] = next;
    i += 1;
  }
  return { flags, positionals };
}

function sanitizeEvidence(text) {
  return String(text ?? "")
    .replace(/[\t\r\n]+/g, " ")
    .trim();
}

function nowTs() {
  return new Date().toISOString();
}

function rowKey(workspace, smell) {
  return `${workspace}\t${smell}`;
}

function parseRow(line, lineNo) {
  const fields = line.split("\t");
  if (fields.length !== 7) {
    throw new Error(`line ${lineNo}: expected 7 tab fields`);
  }
  const [ts, source, workspace, smell, nRaw, action, evidence] = fields;
  if (!ts) {
    throw new Error(`line ${lineNo}: empty ts`);
  }
  if (!SOURCES.has(source)) {
    throw new Error(`line ${lineNo}: invalid source`);
  }
  if (!SLUG.test(workspace)) {
    throw new Error(`line ${lineNo}: invalid workspace`);
  }
  if (!SLUG.test(smell)) {
    throw new Error(`line ${lineNo}: invalid smell`);
  }
  if (!N_RE.test(nRaw)) {
    throw new Error(`line ${lineNo}: invalid n`);
  }
  if (!ACTIONS.has(action)) {
    throw new Error(`line ${lineNo}: invalid action`);
  }
  return {
    ts,
    source,
    workspace,
    smell,
    n: Number(nRaw),
    action,
    evidence,
  };
}

function formatRow(row) {
  return [
    row.ts,
    row.source,
    row.workspace,
    row.smell,
    String(row.n),
    row.action,
    row.evidence,
  ].join("\t");
}

function loadLedger(ledgerPath, { createIfMissing }) {
  if (!existsSync(ledgerPath)) {
    if (createIfMissing) {
      return [];
    }
    throw new Error("missing audit/smells.tsv");
  }
  const raw = readFileSync(ledgerPath, "utf8");
  const lines = raw.split(/\r?\n/);
  while (lines.length > 0 && lines[lines.length - 1] === "") {
    lines.pop();
  }
  if (lines.length === 0) {
    throw new Error("empty audit/smells.tsv");
  }
  if (lines[0] !== HEADER) {
    throw new Error("bad header");
  }
  const rows = [];
  for (let i = 1; i < lines.length; i += 1) {
    rows.push(parseRow(lines[i], i + 1));
  }
  return rows;
}

function writeLedger(ledgerPath, rows) {
  mkdirSync(dirname(ledgerPath), { recursive: true });
  const body = [HEADER, ...rows.map(formatRow)].join("\n") + "\n";
  writeFileSync(ledgerPath, body, "utf8");
}

function requireSlug(name, value) {
  const text = String(value ?? "");
  if (!SLUG.test(text)) {
    fail(`${name} must match [a-z0-9-]+`);
  }
  return text;
}

function requireSource(value) {
  const text = String(value ?? "");
  if (!SOURCES.has(text)) {
    fail("source must be local|grok|cursor-auto");
  }
  return text;
}

function requireEvidence(value) {
  if (value === undefined || value === true) {
    fail("missing --evidence");
  }
  return sanitizeEvidence(value);
}

function findRow(rows, workspace, smell) {
  const key = rowKey(workspace, smell);
  return rows.findIndex((row) => rowKey(row.workspace, row.smell) === key);
}

function applyRecord(rows, input) {
  const idx = findRow(rows, input.workspace, input.smell);
  if (idx === -1) {
    const row = {
      ts: input.ts,
      source: input.source,
      workspace: input.workspace,
      smell: input.smell,
      n: 1,
      action: "note",
      evidence: input.evidence,
    };
    rows.push(row);
    return row;
  }
  const existing = rows[idx];
  const nextN = existing.action === "encode" ? 1 : existing.n + 1;
  const row = {
    ts: input.ts,
    source: input.source,
    workspace: input.workspace,
    smell: input.smell,
    n: nextN,
    action: "note",
    evidence: input.evidence,
  };
  rows[idx] = row;
  return row;
}

function applyEncode(rows, input) {
  const idx = findRow(rows, input.workspace, input.smell);
  if (idx === -1) {
    throw new Error("row missing");
  }
  const existing = rows[idx];
  const row = {
    ...existing,
    ts: input.ts,
    action: "encode",
    evidence: input.evidence,
  };
  rows[idx] = row;
  return row;
}

function printStatus(rows) {
  for (const row of rows) {
    if (row.n >= 2 && row.action === "note") {
      process.stdout.write(
        `REPEAT\t${row.workspace}\t${row.smell}\tn=${row.n}\taction=${row.action}\tsource=${row.source}\t${row.evidence}\n`,
      );
    }
  }
}

function printRow(row) {
  process.stdout.write(`${formatRow(row)}\n`);
}

function cmdDoctor(ledgerPath) {
  try {
    loadLedger(ledgerPath, { createIfMissing: false });
  } catch (err) {
    fail(`doctor: ${err instanceof Error ? err.message : String(err)}`);
  }
  process.stdout.write("ok\n");
}

function cmdStatus(ledgerPath) {
  if (!existsSync(ledgerPath)) {
    return;
  }
  let rows;
  try {
    rows = loadLedger(ledgerPath, { createIfMissing: false });
  } catch {
    return;
  }
  printStatus(rows);
}

function cmdRecord(ledgerPath, flags) {
  const source = requireSource(flags.source);
  const workspace = requireSlug("workspace", flags.workspace);
  const smell = requireSlug("smell", flags.smell);
  const evidence = requireEvidence(flags.evidence);
  let rows;
  try {
    rows = loadLedger(ledgerPath, { createIfMissing: true });
  } catch (err) {
    fail(err instanceof Error ? err.message : String(err));
  }
  const row = applyRecord(rows, {
    ts: nowTs(),
    source,
    workspace,
    smell,
    evidence,
  });
  writeLedger(ledgerPath, rows);
  printRow(row);
}

function cmdEncode(ledgerPath, flags) {
  const workspace = requireSlug("workspace", flags.workspace);
  const smell = requireSlug("smell", flags.smell);
  const evidence = requireEvidence(flags.evidence);
  let rows;
  try {
    rows = loadLedger(ledgerPath, { createIfMissing: false });
  } catch (err) {
    fail(err instanceof Error ? err.message : String(err));
  }
  let row;
  try {
    row = applyEncode(rows, {
      ts: nowTs(),
      workspace,
      smell,
      evidence,
    });
  } catch (err) {
    fail(err instanceof Error ? err.message : String(err));
  }
  writeLedger(ledgerPath, rows);
  printRow(row);
}

function usage() {
  fail(
    [
      "usage:",
      "  node scripts/close-loop.mjs doctor",
      "  node scripts/close-loop.mjs status",
      "  node scripts/close-loop.mjs record --source <local|grok|cursor-auto> --workspace <slug> --smell <slug> --evidence <text>",
      "  node scripts/close-loop.mjs encode --workspace <slug> --smell <slug> --evidence <text>",
    ].join("\n"),
  );
}

const { flags, positionals } = parseFlags(process.argv.slice(2));
const command = positionals[0];
if (!command) {
  usage();
}

const root = resolveFactoryRoot(flags["factory-root"]);
if (!root) {
  fail("factory root not found (walk up for audit/ + docs/SELF-IMPROVE.md, or pass --factory-root)");
}
const ledgerPath = join(root, "audit", "smells.tsv");

switch (command) {
  case "doctor":
    cmdDoctor(ledgerPath);
    break;
  case "status":
    cmdStatus(ledgerPath);
    process.exit(0);
    break;
  case "record":
    cmdRecord(ledgerPath, flags);
    break;
  case "encode":
    cmdEncode(ledgerPath, flags);
    break;
  default:
    usage();
}
