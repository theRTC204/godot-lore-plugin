# godot-lore-plugin

Native in-editor support for Epic Games' [Lore](https://epicgames.github.io/lore/) source control system, in the same place Godot's built-in Git integration lives.

## Setup

1. Copy this `addons/godot-lore-plugin/` folder into your Godot project's `addons/` directory (Windows only for now).
2. Make sure a Lore repository (a `.lore` folder) exists at your **project's root directory** — the same folder as `project.godot`. Lore doesn't search upward for `.lore` the way Git searches for `.git`, so it has to be exactly there.
3. In the editor: **Project → Version Control → Version Control Settings...**, select **LoreVCSPlugin**, and connect.

## What works

- Status (staged / unstaged / conflict state) and working-tree diff, in the Commit dock and diff panel
- Stage, unstage, and discard changes (discarding a never-committed file removes it entirely, matching Git's plugin behavior)
- Commit, with message
- Branch list, current branch, checkout, create, and remove (archive — Lore has no true branch delete)
- Push and pull against the repository's configured remote

## Known limitations

These aren't bugs — they're either genuine gaps in what Lore's API offers versus Git, or scope not built out yet:

- **No commit history view or per-commit diffing.** The Commit dock's history list stays empty; diffing only works against the current working tree, not an arbitrary past revision.
- **No credential/auth UI wiring.** The setup dialog's username/password/SSH fields don't do anything. This works today with a repository that's already authenticated (e.g. via the `lore` CLI, or an unauthenticated local server) — logging in through the editor itself isn't wired up.
- **No "Fetch" button behavior.** Lore's `sync` operation always fetches *and* integrates in one step — there's no Git-style fetch-without-merging to map a separate Fetch action onto, so it's a no-op rather than a surprise full sync.
- **One remote, no "Create/Remove Remote."** Lore has a single server configured per repository, not Git's list of named remotes, so those dock actions are no-ops.

## License

MIT — see `LICENSE`. Bundles [godot-cpp](https://github.com/godotengine/godot-cpp) (statically linked) and [Lore](https://github.com/EpicGames/lore)'s client library (`lore.dll`, loaded at runtime), both also MIT — see `THIRD_PARTY_LICENSES.md`.
