# Characters (Mega-W11 / ADR 0038)

Bundled CC0 humanoid: **`kenney_blocky_character.glb`** (~110 KB) from
[Kenney Blocky Characters 2.0](https://kenney.nl/assets/blocky-characters).
See `LICENSE.txt`.

## Load path

Samples (Sandbox possess, `37_clothing`) call
`engine::assets::CharacterAsset::TryLoadFromCharactersDirOrCapsule` /
`TryLoadGltfOrCapsule`:

1. Prefer `kenney_blocky_character.glb`, else any `*.glb` / `*.gltf` here.
2. On missing or invalid file → procedural capsule via
   `BuildCapsuleCharacterMesh` (same as Mega-W10).

## Refresh / extra skins

Full upstream zip is ~2.1 MB; keep only one small GLB in-repo.

```powershell
powershell -ExecutionPolicy Bypass -File content/characters/download_characters.ps1
```

If fetch fails, keep the committed GLB or regenerate a minimal placeholder and
note it in `LICENSE.txt`.
