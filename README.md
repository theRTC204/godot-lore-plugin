<img src="icon.svg" alt="Godot Lore Plugin logo" style="width:200px; height:200px; object-fit:cover; object-position:50% 50%;" />

# godot-lore-plugin

An in-editor Godot GDExtension bringing native support for Epic Games' [Lore](https://epicgames.github.io/lore/) source control system, mirroring the UX of Godot's built-in Git integration (the first-party `godot-git-plugin`).

The plugin subclasses Godot's `EditorVCSInterface` and drives it via a vendored snapshot of Lore's C API (`lore-capi`/`lore.h`) — the same integration point Epic's own in-progress VS Code plugin is built against. Lore is linked dynamically (`lore.dll` + a small import lib), not statically: the raw staticlib archive `cargo`/`cbindgen` also produce is unlinked and runs into the hundreds of MB to multiple GB (no dead-code elimination happens until something actually links against it), whereas the `cdylib` build is already linked and stripped down to ~30MB. `lore.dll` ships alongside the extension's own DLL as a genuine runtime dependency.

Status: core functionality in place and verified — status, diff, stage/unstage/discard, commit, branch list/current/checkout/create/remove, and push/pull against a real server. See `test_project/addons/godot-lore-plugin/README.md` for what's implemented, current limitations, and how to install the addon itself.

## Repository layout

```
CMakeLists.txt                  Top-level build, targets the "editor" godot-cpp configuration
src/                            GDExtension C++ source
  lore_ffi/                     Thin C++ wrapper bridging lore-capi's async API to synchronous calls
  lore_vcs_plugin.*             LoreVCSPlugin : EditorVCSInterface, the actual VCS plugin
tests/lore_ffi_test.cpp         Standalone console regression harness for lore_ffi (no Godot involved)
third_party/godot-cpp/          Submodule: C++ bindings for Godot's GDExtension API
third_party/lore/               Vendored snapshot: lore.h + lore.dll.lib + lore.dll (see tools/update-lore-snapshot.ps1)
test_project/                   Minimal Godot project for manual testing; also the addon's home
  addons/godot-lore-plugin/     Standard Godot addon layout (this is what would ship — see its own README)
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

Two levels:

- `tests/lore_ffi_test.cpp` (built as `lore_ffi_test`) exercises the `lore_ffi` wrapper directly against a real local Lore repository, no Godot involved — the fastest way to check a change to `src/lore_ffi/` before touching the editor. Run it with no arguments for status/diff, or see its header comment for the `--write-ops-demo`/`--discard-demo`/`--branch-demo`/`--push-demo`/`--pull-demo` regression modes.
- Open `test_project/` in a Godot 4.7 editor build for the real thing: **Project → Version Control → Version Control Settings...**, select `LoreVCSPlugin`, connect. Needs a `.lore` repository at `test_project/`'s root (Lore doesn't search upward for one the way Git does) — not included in this repo; create one with `lore repository create`/`clone`.

## Updating the vendored Lore snapshot

`third_party/lore/` is a point-in-time snapshot, not a live build of the Lore checkout. Refresh it with:

```powershell
tools/update-lore-snapshot.ps1
```

This assumes a sibling checkout of the Lore repository; pass `-LoreRepoPath` to override.

## License

MIT — see `LICENSE`. Bundles [godot-cpp](https://github.com/godotengine/godot-cpp) (statically linked) and [Lore](https://github.com/EpicGames/lore)'s client library (`lore.dll`, loaded at runtime), both also MIT — see `test_project/addons/godot-lore-plugin/THIRD_PARTY_LICENSES.md`.
