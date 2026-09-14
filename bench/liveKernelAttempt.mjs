// bench/liveKernelAttempt.mjs; the plumbing that would let the 11
// kernels this benchmark otherwise marks `not-applicable`
// (consistency, mcmc, numeric-check, dynamics, combinatorial,
// domain-of-validity, order-consistency, boundary-check,
// neuromorphic-power, event-camera-pixel, decision-helper) actually
// ATTEMPT a CVE fixture, via a live model call, instead of never being
// invoked at all.
//
// THIS WAS NOT RUN LIVE AGAINST THE PINNED DATASET. No GEMINI_API_KEY or
// ANTHROPIC_API_KEY was present in the environment this was built in
// (checked directly, not assumed) -- see bench/LIVE-MODEL-PLUMBING.md
// for the full account, including an honest assessment of why running
// this would likely NOT produce a meaningful CVE-detection number even
// with a key, for most of these kernels. This file is real, working,
// tested code (see tests/liveKernelAttempt.test.mjs, using injected fake
// models, no network) -- what it is NOT is a claim that it was exercised
// against a real model or that doing so would validate these kernels as
// vulnerability detectors.
//
// THE GROUNDING PROBLEM, stated once here because it is the actual
// reason this file stops short of being wired into bench/run.mjs's
// default path. Every kernel this repo calls "model designs, device
// executes" has a DEVICE-SIDE step that checks the model's proposal
// against something real and independent of the model: numeric-check
// evaluates an expression at real numbers; mcmc searches a real
// parameter space; combinatorial hands a claim to a from-scratch SAT
// solver. For a NUMERIC or LOGICAL claim, that grounding is genuine.
// For "is this source code file vulnerable to CWE-X," there is no
// equivalent independent evaluator behind consistency/mcmc/dynamics/
// combinatorial/domain-of-validity: the most any of them could do is
// check whether a model's OWN SELF-REPORTED commitments about the code
// are internally consistent with EACH OTHER -- never whether those
// commitments are actually true of the code, because none of these
// kernels re-reads the source. That is verification theater, not
// verification: a "held"/"violated" verdict would reflect the model's
// narrative being self-consistent, not the code being safe. selfAudit.js
// avoids this entirely by pattern-matching the ACTUAL code text, with no
// model in the deciding step at all -- which is exactly why it is the
// one kernel this benchmark's PRIMARY numbers are built on.
//
// WHAT THIS FILE IS ACTUALLY GOOD FOR, honestly: boundary-check and
// chain-reachability are the two exceptions to the grounding problem
// above, because their `run()` steps are pure decision procedures over
// STRUCTURED input (a target/boundary pair; a findings graph) rather
// than free-text commitments -- a model proposing that structured input
// from reading code is a real, if imprecise, extraction step, no
// different in kind from a human doing it. See
// src/lib/shiftLeftScan.js for chain-reachability's DETERMINISTIC
// (no-model) version of exactly that idea, built instead of this one,
// specifically because it needed no live model call to be genuinely
// useful.

import { getKernel, listKernels } from '../src/lib/kernelRegistry.js';
import { runPipeline } from '../src/lib/orchestrator.js';
import { makeLiveDesignSpec } from '../src/lib/modelDesignSpec.js';

// Kernels with a real buildPrompt() adapter in modelDesignSpec.js --
// order-consistency/boundary-check/neuromorphic-power/event-camera-pixel/
// decision-helper have no adapter at all (modelDesignSpec.js returns
// null for them today), so including them would just be silent declines
// dressed up as an attempt. vector-span ALSO has a real adapter now, but
// is deliberately left out of this list: it is scoped to exact
// linear-algebra claims (matrix rank / vector span), not source-code
// vulnerability claims, so offering it here would just be another
// silent decline on every real CVE fixture -- same reasoning, opposite
// direction, as the exclusions above.
const ADAPTED_KERNEL_IDS = ['consistency', 'mcmc', 'numeric-check', 'dynamics', 'combinatorial', 'domain-of-validity'];

export function candidateKernelIdsForLiveAttempt() {
  const registered = new Set(listKernels().map((k) => k.id));
  return ADAPTED_KERNEL_IDS.filter((id) => registered.has(id));
}

/**
 * The ONE new prompt this file adds: given a source file's content, asks
 * whether ANY of the candidate kernels' claim types genuinely applies --
 * "genuinely" meaning stateable as a logical/numeric/combinatorial claim
 * a human could independently check, not "I read the code and think
 * it's vulnerable" (that would be the model doing the analysis itself,
 * with the kernel just rubber-stamping it -- see this file's header).
 */
export function buildCodeClaimPrompt(code, candidateKernelIds) {
  return `You are looking at a source code file, deciding whether ANY of a small set of formal-verification instruments could genuinely check something about it. These instruments check LOGICAL, NUMERIC, or COMBINATORIAL claims -- they are not vulnerability scanners and cannot read code themselves. Your job is narrow: is there a claim about this code's documented behavior (e.g. a stated invariant, a numeric bound, a combinatorial property) that is BOTH (a) actually stateable as a precise logical/numeric/combinatorial claim, AND (b) something one of these kernels could decide independent of trusting your own read of the code?

Available kernels: ${candidateKernelIds.join(', ')}.

If genuinely nothing here fits that description -- which will be the common case for most source files, including most security-relevant ones -- decline. Do not invent a claim just to have something to check; a declined file is the honest, correct answer far more often than an applicable one.

SOURCE FILE:
${code.slice(0, 4000)}

Respond ONLY with JSON:
{"applicable": false}
or
{"applicable": true, "kernelId": "one of the kernels listed above", "claimText": "the precise claim, stateable independent of trusting your own code-reading"}`;
}

export function normalizeCodeClaimResponse(raw, candidateKernelIds) {
  if (!raw || typeof raw !== 'object') throw new Error('code-claim response is not an object');
  if (raw.applicable !== true) return { applicable: false };
  const kernelId = String(raw.kernelId || '').trim();
  const claimText = String(raw.claimText || '').trim();
  if (!candidateKernelIds.includes(kernelId)) throw new Error(`code-claim response named an unlisted kernel "${kernelId}"`);
  if (!claimText) throw new Error('code-claim response marked applicable but gave no claimText');
  return { applicable: true, kernelId, claimText };
}

/**
 * `model` = {extractClaim(code, candidateKernelIds) -> Promise<raw>,
 * designSpec(kernel, claim) -> Promise<rawSpec|null>} -- inject fakes
 * for tests (tests/liveKernelAttempt.test.mjs), or `makeLiveCodeClaimModel`
 * below for the real, model-backed pair.
 *
 * Returns {attempted: false, reason} if the model declined, or
 * {attempted: true, kernelId, result} with a REAL runPipeline() result
 * (through the same worker pool every other kernel call in this repo
 * uses) if it found something to check.
 */
export async function attemptKernelsAgainstCode(code, model, opts = {}) {
  const candidateKernelIds = opts.candidateKernelIds || candidateKernelIdsForLiveAttempt();
  let raw;
  try {
    raw = await model.extractClaim(code, candidateKernelIds);
  } catch (e) {
    return { attempted: false, reason: `claim extraction failed: ${e.message}` };
  }

  let claim;
  try {
    claim = normalizeCodeClaimResponse(raw, candidateKernelIds);
  } catch (e) {
    return { attempted: false, reason: `malformed claim-extraction response: ${e.message}` };
  }
  if (!claim.applicable) return { attempted: false, reason: 'model declined -- no genuinely applicable logical/numeric/combinatorial claim found' };

  let kernel;
  try {
    kernel = getKernel(claim.kernelId);
  } catch (e) {
    return { attempted: false, reason: e.message };
  }

  const report = await runPipeline({ text: claim.claimText }, {
    kernelIds: [kernel.id],
    designSpec: (k, c) => model.designSpec(k, c),
    parallel: opts.parallel,
  });

  const ranEntry = report.ran[0];
  if (ranEntry) return { attempted: true, kernelId: kernel.id, claimText: claim.claimText, verdict: ranEntry.verdict, result: ranEntry.result };
  const skippedEntry = report.skipped[0];
  if (skippedEntry) return { attempted: true, kernelId: kernel.id, claimText: claim.claimText, declined: skippedEntry.reason };
  const failedEntry = report.failed[0];
  return { attempted: true, kernelId: kernel.id, claimText: claim.claimText, failed: failedEntry ? failedEntry.reason : 'unknown failure' };
}

/**
 * Real, model-backed {extractClaim, designSpec} pair. `designSpec` is
 * reused unchanged from modelDesignSpec.js; `extractClaim` is this
 * file's one new prompt/call.
 */
export function makeLiveCodeClaimModel(apiKey, { callJSON, provider = 'gemini', ...callOpts } = {}) {
  const designSpec = makeLiveDesignSpec(apiKey, { callJSON, provider, ...callOpts });
  return {
    designSpec,
    async extractClaim(code, candidateKernelIds) {
      const prompt = buildCodeClaimPrompt(code, candidateKernelIds);
      const doCall = callJSON || (provider === 'anthropic'
        ? (await import('../src/lib/modelClient.js')).callClaudeJSON
        : (await import('../src/lib/geminiClient.js')).callGeminiJSON);
      const { result } = await doCall(apiKey, prompt, callOpts);
      return result;
    },
  };
}
