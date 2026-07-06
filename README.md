<img src="icon.svg" alt="Godot Lore Plugin logo" style="width:200px; height:200px; object-fit:cover; object-position:50% 50%;" />

# godot-lore-plugin

An in-editor Godot GDExtension bringing native support for Epic Games' [Lore](https://epicgames.github.io/lore/) source control system, mirroring the UX of Godot's built-in Git integration (the first-party `godot-git-plugin`).

The plugin subclasses Godot's `EditorVCSInterface` and drives it via a vendored snapshot of Lore's C API (`lore-capi`/`lore.h`) — the same integration point Epic's own in-progress VS Code plugin is built against. Lore is linked dynamically (`lore.dll` + a small import lib), not statically: the raw staticlib archive `cargo`/`cbindgen` also produce is unlinked and runs into the hundreds of MB to multiple GB (no dead-code elimination happens until something actually links against it), whereas the `cdylib` build is already linked and stripped down to ~30MB. `lore.dll` ships alongside the extension's own DLL as a genuine runtime dependency.

Status: early scaffolding. See `docs/plan.md`-equivalent phases below; currently in **Phase 0/1** (build pipeline validation).

## Repository layout

```
CMakeLists.txt                  Top-level build, targets the "editor" godot-cpp configuration
src/                            GDExtension C++ source
third_party/godot-cpp/          Submodule: C++ bindings for Godot's GDExtension API
third_party/lore/               Vendored snapshot: lore.h + lore.dll.lib + lore.dll (see tools/update-lore-snapshot.ps1)
test_project/                   Minimal Godot project for manual testing; also the addon's home
  addons/godot-lore-plugin/     Standard Godot addon layout (this is what would ship)
tools/update-lore-snapshot.ps1  Rebuilds third_party/lore/ from a local Lore checkout
```

## Prerequisites

- CMake 3.19+
- An MSVC toolchain (Visual Studio Build Tools) — Windows is the only supported platform for now
- A locally built Godot editor binary from `M:\Projects\Source\repos\godot` (or another 4.7-compatible editor build) to load the test project against, so the extension is tested against the exact `EditorVCSInterface` API surface it was built for
- Rust + Cargo, only if you need to refresh the vendored Lore snapshot via `tools/update-lore-snapshot.ps1`

## Building

```powershell
git submodule update --init --recursive

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

This produces `test_project/addons/godot-lore-plugin/bin/godot-lore-plugin.windows.editor.x86_64.dll` (referenced by `test_project/addons/godot-lore-plugin/godot-lore-plugin.gdextension`) plus a copy of `lore.dll` alongside it, since the extension DLL depends on it at load time.

## Testing

Open `test_project/` in a Godot 4.7 editor build. The extension should load with no console errors. Later phases require a real local Lore repository (`lore repository create`/`clone`) as a manual test fixture — see the project plan for what to verify at each phase.

## Updating the vendored Lore snapshot

`third_party/lore/` is a point-in-time snapshot, not a live build of the Lore checkout. Refresh it with:

```powershell
tools/update-lore-snapshot.ps1
```

This assumes a sibling checkout of the Lore repository; pass `-LoreRepoPath` to override.
