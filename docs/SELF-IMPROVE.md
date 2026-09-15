# Self-improve loop

This repo carries a close-loop ledger. When a human corrects you, a check
fails, or the same workaround appears again, record it:

```
node scripts/close-loop.mjs record --source devin --workspace <repo-slug> --smell <slug> --evidence <text>
```

`node scripts/close-loop.mjs status` shows repeats; a REPEAT gets encoded
into lint / CI / a hook the same turn, then `encode` on the row.

The loop contract lives in the kitchen: `Dahhrk/dark-factory` ->
`docs/SELF-IMPROVE.md`.
