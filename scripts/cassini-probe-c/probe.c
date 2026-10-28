/* C counterpart of scripts/cassini-probe: the same checks through the C API
 * and this module's c-api.h (with #4032's sortformer member), for every
 * architecture, including those where the Go probe is not built.
 *
 * - The runtime version is the Cassini v2.0.1 string with every marker.
 * - Each model directory transcribes the public English sample at normal
 *   level and x0.001 (-60 dB).
 * - With --diar MODEL WAV EXPECT, Nemotron-3-Diarization runs through
 *   SherpaOnnxOfflineSpeakerSegmentationModelConfig.sortformer and gives the
 *   turns in EXPECT within 0.25 s.
 *
 * Usage: cassini-probe-c WAV [--diar MODEL WAV EXPECT] MODEL_DIR...
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c-api.h"

static const char *kWantVersion =
    "2.0.1+cassini.parakeet-tdt-joiner-v1.nemotron-diarization-v2";
static const char *kMarkers[] = {
    "+cassini.",
    ".parakeet-tdt-joiner-v1",
    ".nemotron-diarization-v2",
};
static const char *kExpected = "ask what you can do for your country";
static const float kTurnTolerance = 0.25f;

static int contains_lower(const char *text, const char *needle) {
  size_t n = strlen(text);
  char *lower = malloc(n + 1);
  for (size_t i = 0; i <= n; ++i) lower[i] = (char)tolower((unsigned char)text[i]);
  int found = strstr(lower, needle) != NULL;
  free(lower);
  return found;
}

/* Returns 0 on success. */
static int diarize(const char *model, const char *wav_path, const char *expect_path) {
  const SherpaOnnxWave *wave = SherpaOnnxReadWave(wav_path);
  if (!wave) {
    printf("FAIL: diarization: cannot read %s\n", wav_path);
    return 1;
  }
  SherpaOnnxOfflineSpeakerDiarizationConfig config;
  memset(&config, 0, sizeof(config));
  config.segmentation.sortformer.model = model;
  config.segmentation.num_threads = 2;
  config.segmentation.provider = "cpu";
  config.min_duration_on = 0.3f;
  config.min_duration_off = 0.5f;
  const SherpaOnnxOfflineSpeakerDiarization *sd =
      SherpaOnnxCreateOfflineSpeakerDiarization(&config);
  if (!sd) {
    printf("FAIL: diarization: creation failed with segmentation.sortformer.model=%s\n", model);
    SherpaOnnxFreeWave(wave);
    return 1;
  }
  int failed = 0;
  if (SherpaOnnxOfflineSpeakerDiarizationGetSampleRate(sd) != wave->sample_rate) {
    printf("FAIL: diarization: model expects %d Hz, wav is %d Hz\n",
           SherpaOnnxOfflineSpeakerDiarizationGetSampleRate(sd), wave->sample_rate);
    failed = 1;
  } else {
    const SherpaOnnxOfflineSpeakerDiarizationResult *r =
        SherpaOnnxOfflineSpeakerDiarizationProcess(sd, wave->samples, wave->num_samples);
    int n = SherpaOnnxOfflineSpeakerDiarizationResultGetNumSegments(r);
    int speakers = SherpaOnnxOfflineSpeakerDiarizationResultGetNumSpeakers(r);
    const SherpaOnnxOfflineSpeakerDiarizationSegment *s =
        SherpaOnnxOfflineSpeakerDiarizationResultSortByStartTime(r);
    for (int i = 0; i < n; ++i) {
      printf("diarization turn %.3f %.3f %d\n", s[i].start, s[i].end, s[i].speaker);
    }
    printf("diarization wav=%s turns=%d speakers=%d\n", wav_path, n, speakers);
    if (speakers < 2) {
      printf("FAIL: diarization found fewer than two speakers\n");
      failed = 1;
    }
    FILE *f = fopen(expect_path, "r");
    if (!f) {
      printf("FAIL: diarization: cannot read %s\n", expect_path);
      failed = 1;
    } else {
      char line[256];
      int i = 0;
      float worst = 0;
      while (fgets(line, sizeof(line), f)) {
        float start, end;
        int speaker;
        if (line[0] == '#' || sscanf(line, "%f %f %d", &start, &end, &speaker) != 3) continue;
        if (i >= n) {
          i++;
          continue;
        }
        float d = fmaxf(fabsf(start - s[i].start), fabsf(end - s[i].end));
        if (d > worst) worst = d;
        if (speaker != s[i].speaker || d > kTurnTolerance) {
          printf("FAIL: diarization turn %d is %.3f-%.3f speaker %d, expected %.3f-%.3f speaker %d\n",
                 i, s[i].start, s[i].end, s[i].speaker, start, end, speaker);
          failed = 1;
        }
        i++;
      }
      fclose(f);
      if (i != n) {
        printf("FAIL: diarization gave %d turns, expected %d\n", n, i);
        failed = 1;
      } else {
        printf("diarization matches %s: max boundary difference %.3f s\n", expect_path, worst);
      }
    }
    SherpaOnnxOfflineSpeakerDiarizationDestroySegment(s);
    SherpaOnnxOfflineSpeakerDiarizationDestroyResult(r);
  }
  SherpaOnnxDestroyOfflineSpeakerDiarization(sd);
  SherpaOnnxFreeWave(wave);
  return failed;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: cassini-probe-c WAV [--diar MODEL WAV EXPECT] MODEL_DIR...\n");
    return 2;
  }
  int first_dir = 2;
  const char *diar_model = NULL, *diar_wav = NULL, *diar_expect = NULL;
  if (strcmp(argv[2], "--diar") == 0) {
    if (argc < 7) {
      fprintf(stderr, "usage: cassini-probe-c WAV [--diar MODEL WAV EXPECT] MODEL_DIR...\n");
      return 2;
    }
    diar_model = argv[3];
    diar_wav = argv[4];
    diar_expect = argv[5];
    first_dir = 6;
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
  if (strcmp(version, kWantVersion) != 0) {
    printf("FAIL: version is \"%s\", expected \"%s\"\n", version, kWantVersion);
    failed = 1;
  }
  for (size_t i = 0; i < sizeof(kMarkers) / sizeof(kMarkers[0]); ++i) {
    if (!strstr(version, kMarkers[i])) {
      printf("FAIL: version lacks marker %s\n", kMarkers[i]);
      failed = 1;
    }
  }
  float *scaled = malloc(sizeof(float) * (size_t)wave->num_samples);
  for (int d = first_dir; d < argc; ++d) {
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
  if (diar_model && diarize(diar_model, diar_wav, diar_expect)) failed = 1;
  if (failed) return 1;
  printf("PASS\n");
  return 0;
}
