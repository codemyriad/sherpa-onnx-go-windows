// Command cassini-probe checks a Cassini native build through this module's Go
// wrapper (the sherpa-onnx v2.0.1-cassini-1 sherpa_onnx.go):
//
//   - the runtime version must be the Cassini v2.0.1 string, with every marker;
//   - each Parakeet model directory must transcribe the public English sample
//     at normal level and attenuated by 60 dB (x0.001). The production
//     Parakeet Ultra bundle keeps the plain Moondream URL in its metadata, so
//     it loads only when TDT is detected from the joiner (stock v2.0.1 exits);
//   - with -diar-model, Nemotron-3-Diarization must run through the wrapper's
//     Segmentation.Sortformer member and give the turns in -diar-expect (the
//     same turns as the Linux x86_64 build of the same source, within a
//     tolerance for compiler and CPU differences).
//
// Usage: cassini-probe -wav WAV [-diar-model M -diar-wav W -diar-expect F] MODEL_DIR...
package main

import (
	"bufio"
	"encoding/binary"
	"flag"
	"fmt"
	"math"
	"os"
	"strings"

	sherpa "github.com/k2-fsa/sherpa-onnx-go/v2/sherpa_onnx"
)

const wantVersion = "2.0.1+cassini.parakeet-tdt-joiner-v1.nemotron-diarization-v2"

var markers = []string{
	"+cassini.",
	".parakeet-tdt-joiner-v1",
	".nemotron-diarization-v2",
}

const expected = "ask what you can do for your country"

// Turn boundaries may move by this much (about three Sortformer frames)
// between builds.
const turnTolerance = 0.25

func readWav(path string) ([]float32, int) {
	b, err := os.ReadFile(path)
	if err != nil {
		panic(err)
	}
	sr := 0
	var pcm []float32
	for i := 12; i+8 <= len(b); {
		id := string(b[i : i+4])
		n := int(binary.LittleEndian.Uint32(b[i+4 : i+8]))
		body := b[i+8 : min(len(b), i+8+n)]
		if id == "fmt " {
			sr = int(binary.LittleEndian.Uint32(body[4:8]))
			if binary.LittleEndian.Uint16(body[0:2]) != 1 || binary.LittleEndian.Uint16(body[2:4]) != 1 || binary.LittleEndian.Uint16(body[14:16]) != 16 {
				panic("need mono s16 PCM")
			}
		}
		if id == "data" {
			for j := 0; j+1 < len(body); j += 2 {
				pcm = append(pcm, float32(int16(binary.LittleEndian.Uint16(body[j:])))/32768)
			}
		}
		i += 8 + n + n%2
	}
	return pcm, sr
}

type turn struct {
	start, end float64
	speaker    int
}

func readTurns(path string) ([]turn, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	var turns []turn
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		var t turn
		if _, err := fmt.Sscanf(line, "%f %f %d", &t.start, &t.end, &t.speaker); err != nil {
			return nil, fmt.Errorf("%s: %q: %v", path, line, err)
		}
		turns = append(turns, t)
	}
	return turns, sc.Err()
}

func diarize(model, wav, expectPath string) bool {
	pcm, sr := readWav(wav)
	cfg := sherpa.OfflineSpeakerDiarizationConfig{MinDurationOn: 0.3, MinDurationOff: 0.5}
	cfg.Segmentation.Sortformer.Model = model
	cfg.Segmentation.NumThreads = 2
	cfg.Segmentation.Provider = "cpu"
	sd := sherpa.NewOfflineSpeakerDiarization(&cfg)
	if sd == nil {
		fmt.Printf("FAIL: diarization: creation failed with Segmentation.Sortformer.Model=%s\n", model)
		return false
	}
	defer sherpa.DeleteOfflineSpeakerDiarization(sd)
	if got := sd.SampleRate(); got != sr {
		fmt.Printf("FAIL: diarization: model expects %d Hz, wav is %d Hz\n", got, sr)
		return false
	}
	var got []turn
	speakers := map[int]bool{}
	for _, s := range sd.Process(pcm) {
		got = append(got, turn{float64(s.Start), float64(s.End), s.Speaker})
		speakers[s.Speaker] = true
		fmt.Printf("diarization turn %.3f %.3f %d\n", s.Start, s.End, s.Speaker)
	}
	fmt.Printf("diarization wav=%s turns=%d speakers=%d\n", wav, len(got), len(speakers))
	ok := true
	if len(speakers) < 2 {
		fmt.Println("FAIL: diarization found fewer than two speakers")
		ok = false
	}
	if expectPath == "" {
		return ok
	}
	want, err := readTurns(expectPath)
	if err != nil {
		fmt.Printf("FAIL: diarization: %v\n", err)
		return false
	}
	if len(want) != len(got) {
		fmt.Printf("FAIL: diarization gave %d turns, expected %d\n", len(got), len(want))
		return false
	}
	worst := 0.0
	for i := range want {
		d := math.Max(math.Abs(want[i].start-got[i].start), math.Abs(want[i].end-got[i].end))
		worst = math.Max(worst, d)
		if want[i].speaker != got[i].speaker || d > turnTolerance {
			fmt.Printf("FAIL: diarization turn %d is %.3f-%.3f speaker %d, expected %.3f-%.3f speaker %d\n",
				i, got[i].start, got[i].end, got[i].speaker, want[i].start, want[i].end, want[i].speaker)
			ok = false
		}
	}
	fmt.Printf("diarization matches %s: max boundary difference %.3f s\n", expectPath, worst)
	return ok
}

func main() {
	wav := flag.String("wav", "", "public English sample (16 kHz mono s16)")
	diarModel := flag.String("diar-model", "", "Nemotron-3-Diarization interface-v2 ONNX model")
	diarWav := flag.String("diar-wav", "", "multi-speaker 16 kHz wav for diarization")
	diarExpect := flag.String("diar-expect", "", "expected turns, one 'start end speaker' per line")
	flag.Parse()
	dirs := flag.Args()
	if *wav == "" || len(dirs) == 0 || (*diarModel == "") != (*diarWav == "") {
		fmt.Fprintln(os.Stderr, "usage: cassini-probe -wav WAV [-diar-model M -diar-wav W [-diar-expect F]] MODEL_DIR...")
		os.Exit(2)
	}
	pcm, sr := readWav(*wav)
	version := sherpa.GetVersion()
	fmt.Printf("sherpa version=%s gitsha=%s wav=%s sr=%d samples=%d\n", version, sherpa.GetGitSha1(), *wav, sr, len(pcm))
	failed := false
	if version != wantVersion {
		fmt.Printf("FAIL: version is %q, expected %q\n", version, wantVersion)
		failed = true
	}
	for _, m := range markers {
		if !strings.Contains(version, m) {
			fmt.Printf("FAIL: version lacks marker %s\n", m)
			failed = true
		}
	}
	for _, d := range dirs {
		cfg := sherpa.OfflineRecognizerConfig{}
		cfg.FeatConfig.SampleRate = 16000
		cfg.FeatConfig.FeatureDim = 128
		cfg.ModelConfig.Transducer.Encoder = d + "/encoder.int8.onnx"
		cfg.ModelConfig.Transducer.Decoder = d + "/decoder.int8.onnx"
		cfg.ModelConfig.Transducer.Joiner = d + "/joiner.int8.onnx"
		cfg.ModelConfig.Tokens = d + "/tokens.txt"
		cfg.ModelConfig.ModelType = "nemo_transducer"
		cfg.ModelConfig.NumThreads = 4
		cfg.ModelConfig.Provider = "cpu"
		cfg.DecodingMethod = "greedy_search"
		r := sherpa.NewOfflineRecognizer(&cfg)
		if r == nil {
			fmt.Printf("FAIL: %s: recognizer creation failed\n", d)
			failed = true
			continue
		}
		for _, gain := range []float32{1, 0.001} {
			x := make([]float32, len(pcm))
			for i, v := range pcm {
				x[i] = v * gain
			}
			s := sherpa.NewOfflineStream(r)
			s.AcceptWaveform(sr, x)
			r.Decode(s)
			res := s.GetResult()
			text := strings.TrimSpace(res.Text)
			fmt.Printf("%s gain=%g tokens=%d text=%q\n", d, gain, len(res.Tokens), text)
			if !strings.Contains(strings.ToLower(text), expected) {
				fmt.Printf("FAIL: %s gain=%g did not transcribe the sample\n", d, gain)
				failed = true
			}
			sherpa.DeleteOfflineStream(s)
		}
		sherpa.DeleteOfflineRecognizer(r)
	}
	if *diarModel != "" && !diarize(*diarModel, *diarWav, *diarExpect) {
		failed = true
	}
	if failed {
		os.Exit(1)
	}
	fmt.Println("PASS")
}
