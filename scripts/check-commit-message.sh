#!/usr/bin/env bash
# Validates a commit subject line against Conventional Commits (type-only, no
# scope enum -- this is a monorepo and one commit/PR can span components).
# Reads the subject line from stdin. Shared by .githooks/commit-msg (local,
# blocking) and the CI commit-lint job (warn-only) so both enforce the same
# rule.
set -euo pipefail

pattern='^(feat|fix|chore|docs|style|refactor|test|build|ci|perf)(\([^)]+\))?: .+'
subject="$(head -n1)"

if [[ "$subject" =~ $pattern ]]; then
  exit 0
fi

cat >&2 <<EOF
Commit message does not follow Conventional Commits:
  $subject

Expected: <type>[(scope)]: <subject>, where <type> is one of:
  feat fix chore docs style refactor test build ci perf

Example: feat(engine): support codec renegotiation via re-INVITE
EOF
exit 1
