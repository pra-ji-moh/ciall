// modelDesignSpec.js; the adapter that lets orchestrator.runPipeline
// drive a MODEL-BACKED kernel (consistency, mcmc, numeric-check,
// dynamics, combinatorial, domain-of-validity, vector-span) live,
// closing the gap VISION-PLTR.md section 8 named explicitly: "a
// model-driven kernel reaching the same live path is still future work."
//
// PROVIDER: defaults to Gemini (current preference; ANTHROPIC_API_KEY
// costs money neither of us needs to spend to prove this path works, and
// Gemini is what's available right now). Pass `provider: 'anthropic'`
// for Claude instead — both go through the identical
// {result, usage, tokens, cost} shape (modelClient.js / geminiClient.js
// mirror each other's interface on purpose), so nothing else in this
// file, or in orchestrator.js, needs to know which one is answering.
//
// WHY THIS IS A SEPARATE FILE FROM modelClient.js. Every kernel's
// buildPrompt() has its OWN call signature — a pre-existing wrinkle in
// this substrate, inherited from client-backend-fixed where each kernel
// was wired into the app's UI with whatever arguments its call site
// already had on hand (mcmc/numeric-check/dynamics/combinatorial take a
// {text, reasoning} "node"; consistency takes claimText/coreClaim/
// siblingClaims as separate strings; domain-of-validity additionally
// needs breakEvidence from a PRIOR instrument's finding, so it declines
// entirely when none is supplied). Rather than pretending buildPrompt is
// uniform, this file is the one place that knows each kernel's real
// signature and adapts a single, uniform `claim` object into it.

// vector-span's buildPrompt(node) uses the identical {text, reasoning}
// signature as mcmc/numeric-check/dynamics (see vectorSpanKernel.js's
// buildVectorSpanPrompt) -- added here deliberately, not left to fall
// through to the "unknown kernel" null-return below, so registering a
// kernel with needsModelExtraction:true actually means its live path
// works, not just that the field says so.
const SIMPLE_NODE_KERNELS = new Set(['mcmc', 'numeric-check', 'dynamics', 'vector-span']);

/**
 * Returns a designSpec(kernel, claim) function suitable for
 * orchestrator.runPipeline's `designSpec` option, backed by a real
 * model call.
 *
 * `claim` shape expected by this adapter: { text, reasoning?, coreClaim?,
 * siblingClaims?, sourceExcerpt?, breakEvidence? } — every field beyond
 * `text` is optional and only used by the kernels that need it.
 *
 * `callJSON` defaults to geminiClient.callGeminiJSON (or
 * modelClient.callClaudeJSON when `provider: 'anthropic'`) but is
 * injectable so this file's own tests never make a real network call —
 * only bin/ciall.mjs, which passes the real one, ever does.
 */
export function makeLiveDesignSpec(apiKey, { callJSON, provider = 'gemini', ...callOpts } = {}) {
  return async function designSpec(kernel, claim) {
    if (typeof kernel.buildPrompt !== 'function') return null; // order-consistency, boundary-check: nothing to design live

    let prompt;
    if (SIMPLE_NODE_KERNELS.has(kernel.id)) {
      prompt = kernel.buildPrompt({ text: claim.text, reasoning: claim.reasoning });
    } else if (kernel.id === 'combinatorial') {
      prompt = kernel.buildPrompt({ text: claim.text, reasoning: claim.reasoning }, claim.sourceExcerpt || '');
    } else if (kernel.id === 'consistency') {
      prompt = kernel.buildPrompt(claim.text, claim.coreClaim || claim.text, claim.siblingClaims || []);
    } else if (kernel.id === 'domain-of-validity') {
      if (!claim.breakEvidence) return null; // this instrument runs AFTER a break is already established; nothing to do without one
      prompt = kernel.buildPrompt(claim.text, claim.coreClaim || claim.text, claim.breakEvidence);
    } else {
      return null; // unknown/future kernel with a buildPrompt this adapter doesn't yet know how to call
    }

    const call = callJSON || (provider === 'anthropic'
      ? (await import('./modelClient.js')).callClaudeJSON
      : (await import('./geminiClient.js')).callGeminiJSON);
    const { result } = await call(apiKey, prompt, callOpts);
    return result;
  };
}
