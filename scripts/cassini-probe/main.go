// Command cassini-probe checks a Cassini native build through the stock Go
// wrapper: the runtime version must carry every Cassini marker, and each
// Parakeet model directory must transcribe the public English sample both at
// normal level and attenuated by 60 dB (x0.001). The attenuated case decodes
// to nothing on the legacy frontend, so it fails unless the reference frontend
// was selected from the model's metadata.
//
// Usage: cassini-probe WAV MODEL_DIR...
package main

import (
	"encoding/binary"
	"fmt"
	"os"
	"strings"

	sherpa "github.com/k2-fsa/sherpa-onnx-go/sherpa_onnx"
)

var markers = []string{
	"+cassini-parakeet-v3-reference-v1",
	".nemotron-diarization-v2",
	".parakeet-ultra-v1",
}

const expected = "ask what you can do for your country"

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

func main() {
	if len(os.Args) < 3 {
		fmt.Fprintln(os.Stderr, "usage: cassini-probe WAV MODEL_DIR...")
		os.Exit(2)
	}
	wav, dirs := os.Args[1], os.Args[2:]
	pcm, sr := readWav(wav)
	version := sherpa.GetVersion()
	fmt.Printf("sherpa version=%s gitsha=%s wav=%s sr=%d samples=%d\n", version, sherpa.GetGitSha1(), wav, sr, len(pcm))
	failed := false
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
	if failed {
		os.Exit(1)
	}
	fmt.Println("PASS")
}
