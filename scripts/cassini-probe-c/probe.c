/* C counterpart of scripts/cassini-probe for targets the stock Go wrapper
 * cannot build (windows/386: sherpa_onnx.go declares 1<<30-element arrays).
 * Same checks through the C API: every Cassini marker in the runtime version,
 * and the public English sample transcribed at normal level and x0.001 for
 * each model directory.
 *
 * Usage: cassini-probe-c WAV MODEL_DIR...
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c-api.h"

static const char *kMarkers[] = {
    "+cassini-parakeet-v3-reference-v1",
    ".nemotron-diarization-v2",
    ".parakeet-ultra-v1",
};
static const char *kExpected = "ask what you can do for your country";

static int contains_lower(const char *text, const char *needle) {
  size_t n = strlen(text);
  char *lower = malloc(n + 1);
  for (size_t i = 0; i <= n; ++i) lower[i] = (char)tolower((unsigned char)text[i]);
  int found = strstr(lower, needle) != NULL;
  free(lower);
  return found;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: cassini-probe-c WAV MODEL_DIR...\n");
    return 2;
  }
  const char *version = SherpaOnnxGetVersionStr();
  const SherpaOnnxWave *wave = SherpaOnnxReadWave(argv[1]);
  if (!wave) {
    fprintf(stderr, "cannot read %s\n", argv[1]);
    return 2;
  }
  printf("sherpa version=%s wav=%s sr=%d samples=%d\n", version, argv[1],
         wave->sample_rate, wave->num_samples);
  int failed = 0;
  for (size_t i = 0; i < sizeof(kMarkers) / sizeof(kMarkers[0]); ++i) {
    if (!strstr(version, kMarkers[i])) {
      printf("FAIL: version lacks marker %s\n", kMarkers[i]);
      failed = 1;
    }
  }
  float *scaled = malloc(sizeof(float) * (size_t)wave->num_samples);
  for (int d = 2; d < argc; ++d) {
    char encoder[1024], decoder[1024], joiner[1024], tokens[1024];
    snprintf(encoder, sizeof(encoder), "%s/encoder.int8.onnx", argv[d]);
    snprintf(decoder, sizeof(decoder), "%s/decoder.int8.onnx", argv[d]);
    snprintf(joiner, sizeof(joiner), "%s/joiner.int8.onnx", argv[d]);
    snprintf(tokens, sizeof(tokens), "%s/tokens.txt", argv[d]);
    SherpaOnnxOfflineRecognizerConfig config;
    memset(&config, 0, sizeof(config));
    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 128;
    config.model_config.transducer.encoder = encoder;
    config.model_config.transducer.decoder = decoder;
    config.model_config.transducer.joiner = joiner;
    config.model_config.tokens = tokens;
    config.model_config.num_threads = 4;
    config.model_config.provider = "cpu";
    config.model_config.model_type = "nemo_transducer";
    config.decoding_method = "greedy_search";
    const SherpaOnnxOfflineRecognizer *recognizer =
        SherpaOnnxCreateOfflineRecognizer(&config);
    if (!recognizer) {
      printf("FAIL: %s: recognizer creation failed\n", argv[d]);
      failed = 1;
      continue;
    }
    const float gains[] = {1.0f, 0.001f};
    for (int g = 0; g < 2; ++g) {
      for (int i = 0; i < wave->num_samples; ++i) scaled[i] = wave->samples[i] * gains[g];
      const SherpaOnnxOfflineStream *stream = SherpaOnnxCreateOfflineStream(recognizer);
      SherpaOnnxAcceptWaveformOffline(stream, wave->sample_rate, scaled, wave->num_samples);
      SherpaOnnxDecodeOfflineStream(recognizer, stream);
      const SherpaOnnxOfflineRecognizerResult *r = SherpaOnnxGetOfflineStreamResult(stream);
      printf("%s gain=%g tokens=%d text=\"%s\"\n", argv[d], gains[g], r->count, r->text);
      if (!contains_lower(r->text, kExpected)) {
        printf("FAIL: %s gain=%g did not transcribe the sample\n", argv[d], gains[g]);
        failed = 1;
      }
      SherpaOnnxDestroyOfflineRecognizerResult(r);
      SherpaOnnxDestroyOfflineStream(stream);
    }
    SherpaOnnxDestroyOfflineRecognizer(recognizer);
  }
  free(scaled);
  SherpaOnnxFreeWave(wave);
  if (failed) return 1;
  printf("PASS\n");
  return 0;
}
