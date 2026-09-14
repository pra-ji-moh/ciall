# Sector positioning — what's real, mapped honestly

This document organizes what actually exists in this repo by the four
sectors it's been discussed for: Wall Street / quant finance, defense
and government (the Palantir-adjacent space), and Silicon Valley /
enterprise tech. It cites only what's built and tested as of this
commit — no adoption, no deployment, no customers are claimed here or
anywhere else in this repo. See `VISION-PLTR.md` for the full narrative
and `SECURITY.md` for the threat model this all sits inside.

## The one-sentence pitch

Nine deterministic/seeded-stochastic verification kernels (SAT solving,
MCMC counterexample search, contradiction detection, dynamics
integration), unified under one registry, applied across domains via
thin typed wrappers — a structurally-separate falsifier for a specific
claim, not another model grading its own output.

## Wall Street / quant finance

- `src/domains/finance.js`: risk-mark reconciliation (two desks' VaR
  marks reconciled via the same kernel that reproduces the published
  Hubble-tension figure), margin stress-testing via MCMC counterexample
  search, risk-ranking cycle detection, and `selectSurvivingCandidates` —
  a mechanical "selection under tail risk" gate built directly against
  Citadel's own quant chief naming that exact bottleneck once AI makes
  generating trade candidates cheap (2026, sourced in `VISION-PLTR.md`
  section 13).
- `src/lib/dataFusion.js`: combines multiple independent measurements of
  the same quantity via inverse-variance weighting — and REFUSES to fuse
  measurements in decisive statistical tension rather than silently
  averaging a real discrepancy into a false consensus number.
- Regulatory angle researched, not yet built against: SR 26-2 (April
  2026, superseding SR 11-7) extends bank model-risk governance to
  LLM/agentic AI specifically. Existing vendors in that space (ModelOp,
  ValidMind, VerifyWise) do governance/inventory; none found doing
  independent adversarial falsification of a specific model assumption,
  which is what the MCMC/consistency kernels actually do.
- **What's not here**: no real market-data feed, no live trading
  integration, no backtesting engine. This verifies stated claims about
  numbers; it does not generate trading signals or execute trades.

## Defense / government (the Palantir-adjacent space)

- `src/domains/missionPlanning.js`: turns a scenario-level "this plan
  failed" into a claim-level, falsifiable operational-envelope check —
  built against Lockheed Martin's own "AI Fight Club" program (a
  scenario-level adversarial simulation arena); this operates one layer
  deeper, explaining WHICH invariant a failing scenario actually violated
  in reproducible, falsifiable terms.
- `src/domains/legal.js`'s `verifyComplianceClaimConsistency`: built
  against a scenario Palantir's OWN blog names as still possible under
  their four-layer ontology architecture — a hallucinated compliance/
  sanction status surviving all four constraint layers because none of
  them independently verify a specific derived claim's logical
  consistency with the facts already on record. Live-verified against
  that exact scenario.
- A concrete, currently-open door researched (not yet pursued): DoD
  SBIR "Runtime Assured Autonomy for AI-Driven Unmanned Platforms" —
  a funded solicitation whose shape (continual assurance of
  learning-enabled cyber-physical systems) matches this architecture's
  pattern more closely than most.
- **What's not here**: no security clearance, no ITAR/classified data
  handling, no government contract vehicle, no relationship with any
  defense prime. This is architecture research, not a program of record.

## Quant firms specifically (distinct from Wall Street generally)

- Same `finance.js` + `dataFusion.js` capability above, plus the
  `adaptiveMcmc.js` domain-bias correction: a human/model-chosen search
  domain that happens to exclude a real counterexample produces a false
  "held" verdict trusted uncritically. `runAdaptiveMcmcSearch` detects
  when a search result hugs its domain's edge and automatically expands
  and retries — live-verified finding a real violation (x≈56) that a
  naively-bounded domain [0,4] would have hidden entirely.
- **What's not here**: no live market microstructure modeling, no
  latency-sensitive execution path (MCMC search is explicitly NOT
  suited to real-time control loops — verification happens at design
  time, the live decision stays a fast deterministic lookup against
  something already proven, per the original research on this).

## Silicon Valley / enterprise tech

- The architecture itself is the pitch here: `kernelRegistry.js` (claim
  verification) and `src/domains/registry.js` (verticals) both follow
  the identical "new capability = new registry entry, not a bespoke
  pipeline" pattern — the actual mechanism behind Palantir's
  land-and-expand model, implemented and tested, not just described.
- Zero external npm dependencies (verified by test) — no supply-chain
  surface from third-party packages, a real, checkable security property
  most enterprise AI tooling can't claim.
- `SECURITY.md`: a threat model that states its own limits as plainly as
  its strengths (e.g., process-launch grants control which binary runs,
  not its semantics — the real boundary is human review of the exact
  command, not the allowlist).
- **What's not here**: no SOC 2, no enterprise SSO/RBAC, no multi-tenant
  deployment, no SLA, no support organization. This is a rigorously
  tested local tool, not an enterprise-ready managed service.

## The honest summary

Every claim above is backed by a test in this repo and, in most cases,
live command output produced in the same session it was built. Nothing
above claims a customer, a deployment, or adoption in any of these four
sectors — that's a distinct, separate thing from what this document
covers, and conflating the two would undercut the credibility of
everything that IS real here.
