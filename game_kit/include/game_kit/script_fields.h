#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace game_kit {

struct ScriptField {
  std::string name;
  std::string type;   // number | string | bool
  std::string value;
};

// Parse `--@export name:type=default` lines from Lua source.
[[nodiscard]] std::vector<ScriptField> ParseScriptExports(std::string_view source);

// `k=v` persist blob used by the editor inspector / Lua `persist` table.
[[nodiscard]] std::string FieldsToPersist(const std::vector<ScriptField>& fields);
void ApplyPersistLine(std::vector<ScriptField>* fields, std::string_view line);
void OverlayPersistBlob(std::vector<ScriptField>* fields, std::string_view blob);

// Merge `--@export` defaults with a persist blob or `{"k":v}` JSON object.
[[nodiscard]] std::string MergeExportsAndPersist(std::string_view source, std::string_view persist);

}  // namespace game_kit
