#pragma once

#include "sim/gameplay.h"
#include "sim/player.h"

#include "engine/rhi/i_device.h"
#include "engine/ui/retained_ui.h"

#include <vector>

namespace mc {

void BuildHud(engine::ui::RetainedUi& ui, const Player& p, const Containers* boxes, int w, int h,
              const HudParams& params, std::vector<engine::rhi::ScreenQuad>* quads,
              std::vector<SlotHit>* hits);

}  // namespace mc
