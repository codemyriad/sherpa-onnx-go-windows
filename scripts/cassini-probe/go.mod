module cassiniprobe

go 1.24.0

require github.com/k2-fsa/sherpa-onnx-go/v2 v2.0.1

require (
	github.com/k2-fsa/sherpa-onnx-go-linux/v2 v2.0.1 // indirect
	github.com/k2-fsa/sherpa-onnx-go-macos/v2 v2.0.1 // indirect
	github.com/k2-fsa/sherpa-onnx-go-windows/v2 v2.0.1 // indirect
)

// Probe the libraries and Go wrapper in this checkout.
replace github.com/k2-fsa/sherpa-onnx-go-windows/v2 => ../..
