#pragma once

#include "editing/settings.h"

#include "game_kit/scene_document.h"

namespace editor {

void PackEditorExtensions(const EditorSettings& settings, game_kit::SceneDocument* doc);
void UnpackEditorExtensions(const game_kit::SceneDocument& doc, EditorSettings* settings);

}  // namespace editor
