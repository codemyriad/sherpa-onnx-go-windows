# Cassini release maintenance

This fork follows
[codemyriad/sherpa-onnx-go-linux](https://github.com/codemyriad/sherpa-onnx-go-linux):
the `v1.13.7-cassini` branch is upstream `v1.13.7` with Cassini's native C API
library, and every release is an immutable `v1.13.7-cassini.N` tag. Never move
a published tag: publish a new version and update the consumer's go.sum.
Consumers pair it with the unmodified `github.com/k2-fsa/sherpa-onnx-go v1.13.7`
wrapper through a `replace` directive.

`v1.13.7-cassini.8` (the first Windows release; its number matches the Linux
module) replaces only `sherpa-onnx-c-api.dll` for x64 and x86. It is built from
[`0c2ee3f3f5f16bb06d234a0f98632810296ba0eb`](https://github.com/codemyriad/sherpa-onnx/commit/0c2ee3f3f5f16bb06d234a0f98632810296ba0eb)
(`v1.13.7-cassini-parakeet-ultra-v1` source tag), the same source as the
Linux `.7`/`.8` libraries, and reports
`1.13.7+cassini-parakeet-v3-reference-v1.nemotron-diarization-v2.parakeet-ultra-v1`:

- the Parakeet v3 reference frontend (`+cassini-parakeet-v3-reference-v1`);
- NVIDIA Nemotron-3-Diarization through the stock `OfflineSpeakerDiarization`
  API with `Segmentation.Pyannote.Model` (`.nemotron-diarization-v2`);
- reference-frontend and TDT selection for Moondream Parakeet Ultra exports
  that keep their own model URL (`.parakeet-ultra-v1`).

As in the Linux builds, text-to-speech is compiled out. The C API keeps all of
upstream's `SherpaOnnx*` exports (the TTS ones report that TTS is unavailable),
so the stock Go wrapper and the stock `sherpa-onnx-cxx-api.dll` (all 150 of its
C API imports) still resolve; only upstream's internal espeak/piper exports are
gone. ONNX Runtime (1.27.1), the C++ wrapper DLLs, the Go source and the C
header are upstream v1.13.7's, unchanged. The ONNX Runtime archive pinned by the
source's CMake files is byte-identical to upstream's `onnxruntime.dll` on both
architectures.

`.github/workflows/cassini-native-windows.yml` builds the DLLs on a GitHub
`windows-latest` runner with MSVC and the static CRT (upstream's default, so the
DLL imports only `KERNEL32.dll` and `onnxruntime.dll`) through
`scripts/build-native.sh`, sherpa-onnx-go-linux's script adapted for Git Bash
and multi-config generators (`-A x64` / `-A Win32`). The same job checks the
PE format, imports and runtime version, then transcribes the public English
sample at normal level and multiplied by `0.001` with Parakeet v3 int8 and the
tpmaerk Parakeet Ultra int8 export with its original metadata, through the C
API (`scripts/cassini-probe-c`, built with MinGW) on both architectures and
through the stock Go wrapper (`scripts/cassini-probe`, MinGW cgo as Go uses)
on x64. All transcripts are the full sentence. The stock `sherpa_onnx.go` does
not compile for `windows/386` (it declares 1<<30-element arrays), so the x86 DLL
is usable from C but not from Go. MSVC output is not byte-reproducible between
runs. The release DLLs come from run
[38673622405](https://github.com/codemyriad/sherpa-onnx-go-windows/actions/runs/38673622405).
Run the workflow from the Actions tab to rebuild; it uploads the DLLs, build
info, `objdump -p` output and probe logs as artifacts.

`CASSINI-SHA256SUMS` records the released DLLs. Verify with
`sha256sum -c CASSINI-SHA256SUMS` from the repository root.
