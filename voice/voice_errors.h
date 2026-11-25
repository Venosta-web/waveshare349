#pragma once
// Human-readable explanations for Assist Pipeline error codes.
//
// Home Assistant only forwards the pipeline error *code* and a short generic
// message to the satellite (e.g. "stt-stream-failed" / "speech-to-text
// failed"). The provider detail — such as Gemini's "503 UNAVAILABLE: model is
// experiencing high demand" or a 429 quota error — stays in the HA log. So the
// display maps each code to what it most likely means and what to do.

#include <string>

namespace voice_errors {

struct Info {
  const char *title;
  const char *body;
  // True when the failure is in a (cloud) provider rather than in what the
  // user said, so retrying on a different pipeline can help.
  bool provider_failure;
};

inline Info describe(const std::string &code) {
  if (code == "stt-stream-failed")
    return {"Speech-to-Text Failed",
            "The STT provider rejected the request. For cloud models this "
            "usually means the model is overloaded (503) or a rate/quota "
            "limit was hit (429). Check the HA log for details.",
            true};
  if (code == "stt-provider-missing" || code == "stt-provider-unsupported-metadata")
    return {"STT Unavailable",
            "The pipeline's speech-to-text engine is not available or does "
            "not support this audio format.",
            true};
  if (code == "stt-no-text-recognized")
    return {"Didn't Catch That", "No speech was recognised. Try again a bit closer to the mic.", false};
  if (code == "intent-failed" || code == "intent-not-supported" ||
      code == "intent-start-failed")
    return {"Assistant Failed",
            "The conversation agent could not answer. A cloud LLM may be "
            "overloaded or out of quota. Check the HA log for details.",
            true};
  if (code == "tts-failed" || code == "tts-not-supported")
    return {"Text-to-Speech Failed", "The answer could not be turned into speech.", false};
  if (code == "pipeline-not-found")
    return {"No Pipeline", "The selected Assist pipeline no longer exists in Home Assistant.", false};
  if (code == "duplicate_wake_up_detected")
    return {"Already Handled", "Another satellite answered this wake word.", false};
  if (code == "pipeline-timeout" || code == "timeout")
    return {"Timed Out", "Home Assistant took too long to respond.", true};
  return {"Voice Error", "", false};
}

// Body text for the overlay: explanation, the raw code and HA's message.
inline std::string body(const std::string &code, const std::string &message) {
  Info info = describe(code);
  std::string out = info.body;
  if (!out.empty())
    out += "\n";
  out += "[" + code + "] " + message;
  return out;
}

}  // namespace voice_errors
