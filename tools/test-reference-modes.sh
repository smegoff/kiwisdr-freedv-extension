#!/usr/bin/env bash
set -u

tester=${1:-/opt/kiwi-freedv-v0-1-13/build/freedv-reference-test}
recording=${2:-/root/FreeDV.test.au}
modes=(1600 700C 700D 700E 2400A 2400B 800XA)
passed=()

for mode in "${modes[@]}"; do
  if "$tester" "$mode" "$recording"; then
    passed+=("$mode")
  fi
done

printf 'reference_modes='
printf '%s ' "${passed[@]}"
printf '\n'
[[ " ${passed[*]} " == *" 700D "* ]]
