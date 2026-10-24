#!/usr/bin/env bash
# Usage: lint_manifest.sh <manifest.toml> [generated_dir]
# Reports perf-relevant codegen settings and hot-spot counts. Read-only. Not run against a real project yet except by hand.
m="$1"; g="$2"
[ -f "$m" ] || { echo "manifest not found: $m"; exit 1; }
echo "== codegen flags (missing = false) =="
for k in skip_lr skip_msr ctr_as_local xer_as_local reserved_as_local cr_as_local non_argument_as_local non_volatile_as_local setjmp_address longjmp_address; do
  v=$(grep -E "^\s*$k\s*=" "$m" | head -1); echo "${k}: ${v:-<unset>}"
done
echo "== [rexcrt] section =="; grep -q '^\[rexcrt\]' "$m" && sed -n '/^\[rexcrt\]/,/^\[/p' "$m" | head -12 || echo "<none> (memcpy/memset run as recompiled PPC)"
echo "== midasm hooks =="; grep -c 'midasm_hook' "$m"
if [ -n "$g" ] && [ -d "$g" ]; then
  echo "== generated code counts (grep over $g) =="
  for p in REX_ENTER_GLOBAL_LOCK REX_CHECK_GLOBAL_LOCK REX_QUERY_TIMEBASE REX_LOOP_BARRIER REX_FATAL __savegprlr __restgprlr; do
    printf '%s: ' "$p"; grep -rho "$p" "$g" --include='*.cpp' 2>/dev/null | wc -l
  done
fi
