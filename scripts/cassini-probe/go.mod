module cassiniprobe

go 1.24.0

require github.com/k2-fsa/sherpa-onnx-go v1.13.7

require (
	github.com/k2-fsa/sherpa-onnx-go-linux v1.13.7 // indirect
	github.com/k2-fsa/sherpa-onnx-go-macos v1.13.7 // indirect
	github.com/k2-fsa/sherpa-onnx-go-windows v1.13.7 // indirect
)

// Probe the libraries in this checkout.
replace github.com/k2-fsa/sherpa-onnx-go-windows => ../..
