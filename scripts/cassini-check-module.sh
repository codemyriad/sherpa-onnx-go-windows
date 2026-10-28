#!/usr/bin/env bash
# Check the libraries this module ships: each lib/<target>/ holds exactly
# Cassini's C API library (sherpa-onnx-c-api.dll) and upstream's ONNX Runtime (onnxruntime.dll), and
# CASSINI-SHA256SUMS lists exactly those files with matching checksums.
#
# Upstream's C++ wrapper library is not shipped: it was built against upstream's
# C header, whose diarization config lacks #4032's Sortformer member, so paired
# with this C API it would pass every field after the segmentation config at the
# wrong offset. Nothing in Go links it.
set -euo pipefail
cd "$(dirname "$0")/.."
if ! command -v sha256sum >/dev/null; then
  sha256sum() { shasum -a 256 "$@"; }
fi
status=0
for dir in lib/*/; do
  for want in sherpa-onnx-c-api.dll onnxruntime.dll; do
    [[ -f $dir$want ]] || { echo "missing $dir$want" >&2; status=1; }
  done
done
present=$(find lib -type f ! -name .gitkeep | LC_ALL=C sort)
while IFS= read -r f; do
  case ${f##*/} in
    sherpa-onnx-c-api.dll|onnxruntime.dll) ;;
    *) echo "unexpected library in module: $f" >&2; status=1 ;;
  esac
done <<<"$present"
listed=$(awk '{print $2}' CASSINI-SHA256SUMS | LC_ALL=C sort)
if [[ $listed != "$present" ]]; then
  echo 'CASSINI-SHA256SUMS does not list exactly the libraries in lib/:' >&2
  diff <(echo "$listed") <(echo "$present") >&2 || true
  status=1
fi
sha256sum -c --quiet CASSINI-SHA256SUMS || status=1
[[ $status == 0 ]] && echo 'module libraries: OK'
exit $status
