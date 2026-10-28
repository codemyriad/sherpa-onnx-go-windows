# Cassini release maintenance

This fork carries the Windows native libraries that Cassini
(codemyriad/gocassini) ships. The `v2.0.1-cassini` branch is upstream
`k2-fsa/sherpa-onnx-go-windows` `v2.0.1` (`2d0a1aa`) with Cassini's C API
DLLs and the Go wrapper of the Cassini sherpa-onnx source; every release is an
immutable `v2.0.1-cassini.N` tag. Never move a published tag: publish a new
version and update the consumer's go.sum.

The module path stays `github.com/k2-fsa/sherpa-onnx-go-windows/v2`, so
consumers pair it with the unmodified `github.com/k2-fsa/sherpa-onnx-go/v2
v2.0.1` package through a `replace` directive:

```
replace github.com/k2-fsa/sherpa-onnx-go-windows/v2 => github.com/codemyriad/sherpa-onnx-go-windows/v2 v2.0.1-cassini.2
```

The `v1.13.7-cassini` branch and its `v1.13.7-cassini.8` tag stay as they were
for consumers of the 1.13.7 runtime.

## v2.0.1-cassini.2

v2.0.1-cassini.1 with upstream's C++ wrapper library
(`lib/*/sherpa-onnx-cxx-api.dll`) removed. The C API library, ONNX
Runtime, `sherpa_onnx.go` and `c-api.h` are byte-identical to v2.0.1-cassini.1.

That wrapper was upstream's stock v2.0.1 build, compiled against upstream's C
header. #4032 appends the `sortformer` member to
`SherpaOnnxOfflineSpeakerSegmentationModelConfig`, so the stock wrapper and this
C API disagree on the layout of the whole speaker diarization config (on 64-bit
the struct grows from 80 to 96 bytes and the embedding config moves from offset
32 to 48). `OfflineSpeakerDiarization::Create` through it would hand the C API
the embedding model path as `segmentation.sortformer.model` and every later
field at the wrong offset, which breaks ordinary Pyannote diarization as well as
Sortformer. Nothing in Go links the C++ wrapper (`build_windows_*.go` link only
`sherpa-onnx-c-api` and `onnxruntime`), so the module now ships only the two
libraries it uses. C++ consumers must build `sherpa-onnx-cxx-api` from the
source commit above.

`scripts/cassini-check-module.sh` (run it from the repository root before
tagging) fails if `lib/` holds anything but the C API library and ONNX
Runtime per architecture, or if `CASSINI-SHA256SUMS` does not list exactly those
files with matching checksums.

## v2.0.1-cassini.1

Built from
[`c810dfcf409b2a72a13e9f0ac961ff55473e350d`](https://github.com/codemyriad/sherpa-onnx/commit/c810dfcf409b2a72a13e9f0ac961ff55473e350d)
(codemyriad/sherpa-onnx tag `v2.0.1-cassini-1`, codeload tarball sha256
`2812247867348e6f22094fa5dd96097b41bdd33ca0be95d94bafcb0f68c4c12c`), the
source that the Linux and macOS forks' `v2.0.1-cassini` branches also pin. It
is upstream sherpa-onnx v2.0.1 plus:

- k2-fsa/sherpa-onnx#4032 (Nemotron-3-Diarization through a Sortformer
  segmentation model, interface version 2);
- TDT detection from the NeMo joiner's output size, so Parakeet Ultra exports
  that keep the plain `https://huggingface.co/moondream/parakeet-ultra` URL
  load (stock v2.0.1 exits with `vocab_size: 8193 != output_size: 8198`);
- the runtime version string
  `2.0.1+cassini.parakeet-tdt-joiner-v1.nemotron-diarization-v2`.

See the source tree's `CASSINI.md` for what each marker means. The 1.13.7
markers `+cassini-parakeet-v3-reference-v1` and `.parakeet-ultra-v1` are gone:
upstream #3999 (in v2.0.1) replaced the Parakeet reference frontend.

What changed in this module against upstream v2.0.1:

- `lib/{x86_64,i686,aarch64}-pc-windows-gnu/sherpa-onnx-c-api.dll`: built
  from the source above. Text-to-speech is compiled out, as in the Linux
  builds; the C API keeps all of upstream's `SherpaOnnx*` exports (the TTS ones
  report that TTS is unavailable).
- `sherpa_onnx.go` and `c-api.h`: copied from the source tree
  (`scripts/go/sherpa_onnx.go`, `sherpa-onnx/c-api/c-api.h`). Against upstream
  v2.0.1 they add only #4032's `OfflineSpeakerSegmentationModelConfig.Sortformer`
  (C: `SherpaOnnxOfflineSpeakerSegmentationModelConfig.sortformer`, appended to
  the struct). Pass the Nemotron model in `Segmentation.Sortformer.Model`; the
  1.13.7 fork's `Segmentation.Pyannote.Model` route does not exist here.
- ONNX Runtime 1.28.2 (`onnxruntime.dll`) is upstream v2.0.1's, unchanged.
  v2.0.1-cassini.1 also kept upstream's `sherpa-onnx-cxx-api.dll`;
  v2.0.1-cassini.2 removes it (see above). The ONNX Runtime archive pinned by the source's
  CMake files (`onnxruntime-win-{x64,x86,arm64}-MT-Release-1.28.2`) gave a DLL
  byte-identical to upstream's on all three architectures.

`.github/workflows/cassini-native-windows.yml` builds the three DLLs on GitHub
`windows-latest` runners (image windows-2025-vs2026, MSVC 19.51) with the
static CRT (upstream's default, so each DLL imports only `KERNEL32.dll` and
`onnxruntime.dll`) through `scripts/build-native.sh` (`-A x64`, `-A Win32`,
`-A ARM64`; ARM64 is cross-compiled, as upstream builds it). The build job
checks the PE machine type and imports (`scripts/cassini-pe-check.py`) and the
runtime version. A probe job per architecture then runs on a native runner
(`windows-latest` for x64 and x86, `windows-11-arm` for ARM64) through the C
API (`scripts/cassini-probe-c`, built with MinGW GCC or LLVM-MinGW clang) and
through this module's Go wrapper (`scripts/cassini-probe`, cgo as Go uses it):

- the runtime version equals the string above;
- the public English sample (`en.wav`), at normal level and multiplied by
  `0.001` (-60 dB), transcribes to the full sentence (27 tokens) with Parakeet
  v3 int8 (k2-fsa export) and with Cassini's production Parakeet Ultra int8
  bundle from dist.gocassini.com (`parakeet-ultra-0.6b-int8`, revision
  `18cb6230…`, encoder sha256 `299617fe…`, plain Moondream URL);
- Nemotron-3-Diarization int8 (`nemotron-3-diarization-int8`, interface
  version 2, sha256 `47c221ea…`) through the `Sortformer` member on the public
  four-speaker clip (`0-four-speakers-zh.wav`) finds 4 speakers in 9 turns,
  matching `scripts/cassini-probe/four-speakers-expected.txt` (the Linux
  x86_64 GCC build of the same source) within 0.03 s on every turn.

All checks pass on x64, x86 and ARM64, from C and from Go (the v2.0.1 wrapper
now compiles for `windows/386`; the ARM64 Go probe ran natively with Go
1.24 windows/arm64 and LLVM-MinGW clang 22). MSVC output is not
byte-reproducible between runs. The release DLLs come from run
[40705537678](https://github.com/codemyriad/sherpa-onnx-go-windows/actions/runs/40705537678).
Run the workflow from the Actions tab, or push a change to its inputs on this
branch, to rebuild; it uploads the DLLs, build info, import check and probe
logs as artifacts.

Not validated here: the fp32 Ultra and fp32 diarization models, beam search,
and GPU execution providers (these DLLs are CPU only).

`CASSINI-SHA256SUMS` records every DLL in the module. Verify with
`sha256sum -c CASSINI-SHA256SUMS` from the repository root.
