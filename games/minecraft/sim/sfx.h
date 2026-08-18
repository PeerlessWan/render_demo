#pragma once

#include "engine/media/media.h"

#include <memory>

namespace mc {

struct Sfx {
  std::unique_ptr<engine::media::IAudioDevice> device;
  engine::media::AudioClip click;
  engine::media::AudioClip hurt;
  bool ready = false;
};

void InitSfx(Sfx* sfx);
void PlayClick(Sfx* sfx);
void PlayHurt(Sfx* sfx);

}  // namespace mc
