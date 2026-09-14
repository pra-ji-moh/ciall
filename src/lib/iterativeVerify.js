// iterativeVerify.js; scaffolding for an ITERATIVE, tool-using
// verification loop over ciall-substrate's kernels, instead of the
// single-pass model-designs-one-spec path bin/ciall.mjs's `--live`
// already has. The premise, stated directly: published research on this
// task class found that an iterative agentic workflow with real tool
// access closes most of the capability gap between small and large
// models on vulnerability detection — more than raw model scale does.
// The lever this file builds is WORKFLOW, not a bigger single model call.
//
// WHAT THIS REUSES, UNCHANGED. `modelDesignSpec.js`'s `designSpec(kernel,
// claim)` — the exact single-pass adapter `--live` already uses — is the
// thing that turns a chosen kernel + claim into a real spec; this file
// calls it again on every round instead of forking a second copy of its
// per-kernel prompt-building logic. `orchestrator.js`'s `runPipeline` is
// the thing that actually EXECUTES a round's spec (normalize -> run,
// through the same worker pool every other kernel call in this repo
// goes through) — every round is one `runPipeline` call with a single
// kernelId, not a new execution path. `selfAudit.js`'s `scanCodeText`
// (extracted from `scanFile` specifically for this file's use — see that
// file's header) is the red-flag gate applied to every single piece of
// text a model hands back, before this loop does anything with it.
//
// WHAT IS GENUINELY NEW: the round-to-round control flow, and exactly one
// new prompt (`buildReflectionPrompt`) — the "look at what you checked
// and decide" step that does not exist anywhere else in this codebase,
// because nothing else here ever asked a model to look at ITS OWN PRIOR
// verification result and decide what to do next.
//
// THE ONE RULE THIS FILE EXISTS TO ENFORCE: a model may never move from
// "unconfirmed" to "confirmed" on the strength of restating its own
// opinion more confidently. Every "revise" MUST produce a genuinely new
// runPipeline() call against a real kernel before the next round's
// reflection is even asked anything (see the loop body — there is no
// path from "revise" back to another reflection without an intervening
// kernel run). And "accept" is refused, structurally, overridden by this
// file rather than trusted, if history contains not one single round
// where a kernel actually produced a result — the model does not get the
// last word on whether its own accept was earned, the same "a structural
// contradiction beats the stated verdict" discipline domainOfValidity.js
// already applies to a claimed "narrowed" whose own fields contradict it.
//
// WHAT THIS FILE NEVER DOES: touch a file, run a command, or call
// confirm() itself. A "act" decision is a TERMINAL state this loop
// reports and stops at — `{verdict:'awaiting-human-action', proposedAction}`
// — the caller decides whether to actually invoke deviceExecutor.js's
// writeFile/commandExecutor.js's runCommand (both already require their
// own fresh confirm() per this repo's standing rule); this file is not
// where that gate lives and does not reimplement it.

import { getKernel, listKernels } from './kernelRegistry.js';
import { runPipeline } from './orchestrator.js';
import { scanCodeText } from './selfAudit.js';
import { makeLiveDesignSpec } from './modelDesignSpec.js';

export const DEFAULT_MAX_ROUNDS = 4;
export const HARD_MAX_ROUNDS = 10; // never exceeded regardless of what a caller passes -- the literal "hard max" the spec requires
export const DEFAULT_CANDIDATE_KERNEL_IDS = ['consistency', 'mcmc', 'numeric-check', 'dynamics']; // same as bin/ciall.mjs's CORE_LIVE_KERNELS, so round 1 here matches the existing --live default exactly

const VALID_DECISIONS = new Set(['accept', 'revise', 'act']);

// ── the red-flag gate ─────────────────────────────────────────────────

/**
 * Runs selfAudit.js's real pattern rules (no whitelist -- see that
 * file's header on why none applies here) against ANY value a model
 * handed back, by stringifying it first. Called on every reflection
 * response and every designed spec, before this loop uses either for
 * anything. A non-empty return means: stop, do not proceed, flag for
 * human review -- never "try to sanitize and continue."
 */
export function scanForRedFlags(value) {
  const text = typeof value === 'string' ? value : JSON.stringify(value ?? null);
  return scanCodeText(text, text);
}

// ── reflection: the one genuinely new prompt ──────────────────────────

function summarizeRoundForPrompt(h) {
  const bits = [`Round ${h.round}: kernel="${h.kernelId}".`];
  if (h.declined) bits.push(`declined: ${h.declined}`);
  else if (h.failed) bits.push(`failed: ${h.failed}`);
  else if (h.result != null) bits.push(`verdict=${h.verdict}, result=${JSON.stringify(h.result).slice(0, 600)}`);
  if (h.reflection?.rationale) bits.push(`(chosen because: ${h.reflection.rationale})`);
  return bits.join(' ');
}

/**
 * The ONE new prompt this file adds. Presents the claim and the FULL
 * round trace so far, and asks the model to decide accept / revise /
 * act -- see this file's header for why "accept" and "revise" are each
 * constrained the way they are.
 */
export function buildReflectionPrompt(claim, history) {
  const kernelMenu = listKernels().filter((k) => typeof k.buildPrompt === 'function').map((k) => k.id).join(', ');
  const roundsText = history.map(summarizeRoundForPrompt).join('\n');
  return `You are reviewing an automated, in-progress verification against this claim. You do NOT get to just restate your opinion -- you must decide whether the checks so far are enough, or propose a genuinely new one.

CLAIM: ${claim.text}

CHECKED SO FAR:
${roundsText || '(nothing yet)'}

Decide exactly one of:
- "accept": the checks above are sufficient and you are done. Only valid if at least one round above actually produced a real kernel result (not a decline or failure) -- an accept with nothing backing it will be refused.
- "revise": propose the NEXT deterministic check. Either the SAME kernel with a narrower or different spec (say plainly what the prior pass missed, or why it was too broad), or a genuinely different kernel from: ${kernelMenu}. Name it via "kernelId" -- you are choosing WHICH check runs next, not writing its spec; that is designed separately in the next step.
- "act": you believe a real file write or command execution should happen now. Describe it in "actionProposal", in plain terms. This will NEVER be executed automatically by this step -- a human reviews and confirms it separately, through this repo's existing confirm() gate.

Respond ONLY with JSON:
{"decision":"accept","rationale":"one or two sentences"}
or
{"decision":"revise","kernelId":"mcmc","rationale":"one or two sentences explaining what the next check should cover that the prior one did not"}
or
{"decision":"act","actionProposal":{"kind":"write-file or run-command, in plain words","detail":"..."},"rationale":"one or two sentences"}`;
}

/** Validates+clamps a model's reflection response. Throws on malformed input, same discipline as every normalize() in this repo. */
export function normalizeReflection(raw) {
  if (!raw || typeof raw !== 'object') throw new Error('reflection is not an object');
  if (!VALID_DECISIONS.has(raw.decision)) throw new Error(`unknown reflection decision "${raw.decision}"`);
  const rationale = String(raw.rationale || '').slice(0, 1000);
  if (raw.decision === 'revise') {
    const kernelId = String(raw.kernelId || '').trim();
    if (!kernelId) throw new Error('decision "revise" needs a non-empty kernelId');
    return { decision: 'revise', kernelId, rationale };
  }
  if (raw.decision === 'act') {
    const actionProposal = raw.actionProposal && typeof raw.actionProposal === 'object' ? raw.actionProposal : {};
    return { decision: 'act', actionProposal, rationale };
  }
  return { decision: 'accept', rationale };
}

// ── the loop ───────────────────────────────────────────────────────────

async function executeRound(kernelId, roundClaim, model, parallel) {
  let kernel;
  try {
    kernel = getKernel(kernelId);
  } catch (e) {
    return { kernelId, declined: `unknown kernel: ${e.message}` };
  }

  const redFlags = scanForRedFlags(roundClaim);
  if (redFlags.length > 0) return { kernelId, redFlags };

  const report = await runPipeline(roundClaim, {
    kernelIds: [kernelId],
    designSpec: (k, c) => model.designSpec(k, c),
    parallel,
  });

  const ranEntry = report.ran[0];
  if (ranEntry) {
    const specRedFlags = scanForRedFlags(ranEntry.spec);
    if (specRedFlags.length > 0) return { kernelId, redFlags: specRedFlags };
    return { kernelId, spec: ranEntry.spec, result: ranEntry.result, verdict: ranEntry.verdict };
  }
  const skippedEntry = report.skipped[0];
  if (skippedEntry) return { kernelId, declined: skippedEntry.reason };
  const failedEntry = report.failed[0];
  return { kernelId, failed: failedEntry ? failedEntry.reason : 'unknown failure' };
}

function finalize(verdict, history, claim, extra = {}) {
  const lastBacked = [...history].reverse().find((h) => h.result != null);
  return {
    verdict,
    claim,
    rounds: history,
    finalKernelId: lastBacked ? lastBacked.kernelId : null,
    finalResult: lastBacked ? lastBacked.result : null,
    ...extra,
  };
}

/**
 * Runs the full iterative loop. `opts.model` = {designSpec(kernel,claim),
 * reflect({claim,history})} — inject a fake, deterministic pair for
 * tests (see tests/iterativeVerify.test.mjs); `makeLiveIterativeModel`
 * below builds the real, model-backed pair for actual use.
 *
 * Returns {verdict, claim, rounds, finalKernelId, finalResult, ...}.
 * `verdict` is one of:
 *   'confirmed'              — the model accepted, backed by a real result
 *   'unconfirmed-max-rounds' — hit the cap without an accept; never a
 *                              silent escalation of confidence
 *   'awaiting-human-action'  — a round proposed a real action; this loop
 *                              stopped without executing anything at all
 *   'terminated-red-flag'    — a model response tripped selfAudit's
 *                              rules; flagged for human review, not
 *                              continued
 */
export async function runIterativeVerification(claim, opts = {}) {
  const model = opts.model;
  if (!model || typeof model.designSpec !== 'function' || typeof model.reflect !== 'function') {
    throw new Error('runIterativeVerification needs opts.model = {designSpec(kernel,claim), reflect({claim,history})}');
  }
  if (!claim || typeof claim.text !== 'string' || !claim.text) {
    throw new Error('runIterativeVerification needs a claim with a non-empty text string');
  }
  const maxRounds = Math.min(Math.max(1, Number(opts.maxRounds) || DEFAULT_MAX_ROUNDS), HARD_MAX_ROUNDS);
  const candidateKernelIds = opts.candidateKernelIds && opts.candidateKernelIds.length > 0 ? opts.candidateKernelIds : DEFAULT_CANDIDATE_KERNEL_IDS;

  const history = [];
  let round = 1;
  const r1 = await executeRound(candidateKernelIds[0], claim, model, opts.parallel);
  history.push({ round, action: 'kernel-check', ...r1 });
  if (r1.redFlags) return finalize('terminated-red-flag', history, claim);

  while (round < maxRounds) {
    round++;

    let rawReflection;
    try {
      rawReflection = await model.reflect({ claim, history });
    } catch (e) {
      history.push({ round, action: 'reflect-error', error: e.message });
      break; // cannot decide what to do next without a reflection -- stop, report unconfirmed rather than guess
    }

    const reflectionRedFlags = scanForRedFlags(rawReflection);
    if (reflectionRedFlags.length > 0) {
      history.push({ round, action: 'reflect', raw: rawReflection, redFlags: reflectionRedFlags });
      return finalize('terminated-red-flag', history, claim);
    }

    let reflection;
    try {
      reflection = normalizeReflection(rawReflection);
    } catch (e) {
      history.push({ round, action: 'reflect-invalid', raw: rawReflection, error: e.message });
      continue; // a malformed reflection still consumes a round -- no free retries against the cap
    }

    if (reflection.decision === 'act') {
      history.push({ round, action: 'act', reflection });
      return finalize('awaiting-human-action', history, claim, { proposedAction: reflection.actionProposal });
    }

    if (reflection.decision === 'accept') {
      const hasBackingResult = history.some((h) => h.result != null);
      if (!hasBackingResult) {
        history.push({ round, action: 'accept-overridden', reflection, reason: 'no round so far produced an actual kernel result to back this -- accept refused, the model does not get the last word here' });
        continue;
      }
      history.push({ round, action: 'accept', reflection });
      return finalize('confirmed', history, claim);
    }

    // decision === 'revise': MUST produce a new, real kernel-verified
    // result before the next reflection is asked anything -- see this
    // file's header for why that is the one rule this loop exists to
    // enforce.
    let kernelExists = true;
    try { getKernel(reflection.kernelId); } catch { kernelExists = false; }
    if (!kernelExists) {
      history.push({ round, action: 'revise-invalid-kernel', reflection });
      continue;
    }

    const enrichedClaim = { ...claim, reasoning: [claim.reasoning, reflection.rationale].filter(Boolean).join('\n\n') };
    const rN = await executeRound(reflection.kernelId, enrichedClaim, model, opts.parallel);
    history.push({ round, action: 'kernel-check', reflection, ...rN });
    if (rN.redFlags) return finalize('terminated-red-flag', history, claim);
  }

  return finalize('unconfirmed-max-rounds', history, claim);
}

// ── live adapter ───────────────────────────────────────────────────────

/**
 * Builds the real, model-backed {designSpec, reflect} pair for actual
 * use — mirrors modelDesignSpec.js's makeLiveDesignSpec exactly for the
 * `designSpec` half (reused, not reimplemented) and adds only the
 * `reflect` half this file introduces. Same provider mirroring
 * (`callGeminiJSON`/`callClaudeJSON`) and same "apiKey passed as a
 * parameter, never read from process.env here" discipline as
 * modelDesignSpec.js.
 */
export function makeLiveIterativeModel(apiKey, { callJSON, provider = 'gemini', ...callOpts } = {}) {
  const designSpec = makeLiveDesignSpec(apiKey, { callJSON, provider, ...callOpts });
  return {
    designSpec,
    async reflect({ claim, history }) {
      const prompt = buildReflectionPrompt(claim, history);
      const doCall = callJSON || (provider === 'anthropic'
        ? (await import('./modelClient.js')).callClaudeJSON
        : (await import('./geminiClient.js')).callGeminiJSON);
      const { result } = await doCall(apiKey, prompt, callOpts);
      return result;
    },
  };
}
