#!/usr/bin/env bash
# Fetches the triggering PR's diff, sends it to a Gemini Flash model (free
# AI Studio API key -- no billing account, rate-limited not billed, see root
# AGENTS.md / the PR that introduced this workflow), and posts the response
# back as a PR comment. Invoked only by .github/workflows/gemini-review.yml,
# only from a "/gemini-review" comment authored by the project lead.
set -euo pipefail

: "${PR_NUMBER:?PR_NUMBER is required}"
: "${GEMINI_API_KEY:?GEMINI_API_KEY is required}"

MAX_DIFF_CHARS=60000
DIFF_FILE="$(mktemp)"
PROMPT_FILE="$(mktemp)"
PAYLOAD_FILE="$(mktemp)"
RESPONSE_FILE="$(mktemp)"
trap 'rm -f "$DIFF_FILE" "$PROMPT_FILE" "$PAYLOAD_FILE" "$RESPONSE_FILE"' EXIT

gh pr diff "$PR_NUMBER" > "$DIFF_FILE"

if [[ $(wc -c < "$DIFF_FILE") -gt $MAX_DIFF_CHARS ]]; then
  head -c "$MAX_DIFF_CHARS" "$DIFF_FILE" > "${DIFF_FILE}.trunc"
  mv "${DIFF_FILE}.trunc" "$DIFF_FILE"
  echo -e "\n\n[diff truncated at ${MAX_DIFF_CHARS} characters]" >> "$DIFF_FILE"
fi

cat > "$PROMPT_FILE" <<'EOF'
You are doing an on-demand deep-dive code review for the SBC project (a SIP
B2BUA in C++26/PJSIP/Boost.SML, plus an Express/TS + React control plane).
This review was explicitly requested by a human because the PR looks like it
might touch something architecturally sensitive -- so focus your attention
on:

- Boost.SML state machine correctness: no re-entrant process_event() calls
  from inside an action (must use the machine's self-fire queue instead);
  transitions that leave the machine unable to reach a terminal state.
- CallManager/CallSession lifetime: a CallSession must never be deleted from
  inside its own SM action (only via CallManager::schedule_remove() /
  purge_scheduled()); actions objects must be declared/outlive before the SM
  runners that reference them.
- MediaBridge and the RTP relay: the only place with genuine cross-thread
  shared state (SIP thread vs. the RTP io_context thread) -- flag any new
  shared mutable state without atomic/mutex protection, and any call into
  MediaBridge's setup methods after start_bridge_loop() has been invoked.
- SDP/PJSIP parsing correctness against malformed or adversarial input
  (this code has no unit test coverage yet, so be more thorough here, not
  less).
- Real security or correctness bugs -- not style, naming, or formatting
  (those are already gated by clang-tidy/eslint/CI).

Be concise. If you find nothing in-scope, say so plainly in one line rather
than padding the review. Here is the PR diff:

EOF
cat "$DIFF_FILE" >> "$PROMPT_FILE"

jq -n --rawfile prompt "$PROMPT_FILE" \
  '{contents: [{parts: [{text: $prompt}]}]}' \
  > "$PAYLOAD_FILE"

http_status=$(curl -sS -o "$RESPONSE_FILE" -w '%{http_code}' \
  "https://generativelanguage.googleapis.com/v1beta/models/gemini-flash-latest:generateContent?key=${GEMINI_API_KEY}" \
  -H 'Content-Type: application/json' \
  --data-binary @"$PAYLOAD_FILE")

if [[ "$http_status" != "200" ]]; then
  echo "Gemini API request failed (HTTP $http_status):" >&2
  cat "$RESPONSE_FILE" >&2
  gh pr comment "$PR_NUMBER" --body "**/gemini-review failed** -- the Gemini API request returned HTTP $http_status. Check the workflow run logs."
  exit 1
fi

review_text=$(jq -r '.candidates[0].content.parts[0].text // empty' "$RESPONSE_FILE")

if [[ -z "$review_text" ]]; then
  echo "Gemini returned no review text. Raw response:" >&2
  cat "$RESPONSE_FILE" >&2
  gh pr comment "$PR_NUMBER" --body "**/gemini-review failed** -- Gemini returned an empty response. Check the workflow run logs."
  exit 1
fi

{
  echo "**Gemini review** (on-demand, requested via \`/gemini-review\`)"
  echo
  echo "$review_text"
} | gh pr comment "$PR_NUMBER" --body-file -
