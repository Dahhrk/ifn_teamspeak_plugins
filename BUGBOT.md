# Bugbot / agent review bans

Aligned with the gardener loop + Dune contract. Soft layer — promote repeats into hard CI.

## Hard preferences (ask for CI if missing)

- Forbidden cross-layer / cross-feature imports must fail the build, not only a comment.
- Durable state has one obvious writer.
- New work adds isolated files; avoid editing shared god registries.
- Exceptions are architecture PRs, not silent `any` / suppressions.
- UI products: `anti-ai-ui` (+ visual-parity when baselines exist) must stay green.

## Ban / flag

- Comments that invent policy ("never do X") from a one-off review note
- `any` without a named escape hatch
- Lint suppressions added without a ticket / architecture note
- Hand-rolled UI primitives when the design system already has one
- "Fixed" with no runtime evidence (screenshot, test, control CLI, trace)
- Expanding a Feature Map to paper over a product bug (report the bug instead)
- AI-template UI: Inter/Roboto/system primary, purple/indigo tropes, nightglass (dark + grain + neon mint/cyan), unnamed "nice dark UI"

## PR shape

- One verifiable unit per PR
- Author agent does not merge on its own verdict
- Draft until before/after proof exists
- Second smell of the same UI trope → extend `check-anti-ai-ui.mjs` (or titled visual baseline PR), not more prose

See product `dune.md` and kitchen `docs/dune-method.md` · `docs/SELF-IMPROVE.md`.
