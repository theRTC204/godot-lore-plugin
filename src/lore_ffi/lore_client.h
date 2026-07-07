#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lore_ffi {

enum class FileAction {
	Keep,
	Add,
	Delete,
	Move,
	Copy,
};
// Note: there is no "Modify" action. A content change to an existing,
// otherwise-untouched file is Keep + FileStatus::dirty == true; verified
// empirically against `lore status`, whose "M" display label is purely a
// presentation choice over that same (Keep, dirty) combination. A later
// phase mapping this onto Godot's EditorVCSInterface::ChangeType must
// derive CHANGE_TYPE_MODIFIED from (action == Keep && dirty), not from a
// dedicated action value.

// One file's status, as reported by `lore_repository_status`. Field names
// mirror lore_repository_status_file_event_data_t; see Godot's
// editor_vcs_interface.h (ChangeType / TreeArea) for how a later phase maps
// these onto EditorVCSInterface's status model.
struct FileStatus {
	std::string path;
	std::string from_path; // non-empty when action is Move/Copy
	uint64_t size = 0;
	FileAction action = FileAction::Keep;

	bool staged = false;
	bool merged = false;
	bool conflict = false;
	bool conflict_unresolved = false;
	bool conflict_automerged = false;
	bool conflict_mine = false;
	bool conflict_theirs = false;
	bool dirty = false;
};

// One file's diff, as reported by `lore_file_diff`.
struct FileDiff {
	std::string path;
	std::string patch; // unified-diff text
	FileAction action = FileAction::Keep;
};

// Reports success/failure and, on failure, the message from Lore's own
// error reporting (see lore_error_detail_t / lore_complete_event_data_t).
struct LoreResult {
	bool ok = true;
	std::string error_message;
};

// Thin C++ wrapper around lore-capi (third_party/lore/include/lore.h),
// adapting its asynchronous, event-callback API (see LoreCall) to the
// synchronous calls EditorVCSInterface's virtual methods need.
class LoreClient {
public:
	// Must be called once before any repository operation; wraps
	// lore_log_configure. Safe to call more than once.
	static void initialize();

	// Must be called once when no further Lore calls will be made; wraps
	// lore_shutdown.
	static void shutdown();

	// Reports the working-tree status of `p_repository_path` (a directory
	// containing a `.lore` repository).
	static LoreResult status(const std::string &p_repository_path, std::vector<FileStatus> &r_files);

	// Reports the unified diff for `p_paths` in `p_repository_path`, of the
	// working tree against the current revision (empty `p_paths` diffs
	// everything).
	static LoreResult diff(const std::string &p_repository_path, const std::vector<std::string> &p_paths, std::vector<FileDiff> &r_diffs);

	// Stages `p_paths` for the next commit.
	static LoreResult stage(const std::string &p_repository_path, const std::vector<std::string> &p_paths);

	// Unstages `p_paths`, moving them back to the working-tree state.
	static LoreResult unstage(const std::string &p_repository_path, const std::vector<std::string> &p_paths);

	// Discards local changes to `p_paths`, resetting them to the current
	// revision. Always purges untracked paths too (not just modified
	// tracked ones): EditorVCSInterface's "discard changes" action is
	// expected to remove a brand-new, never-committed file entirely, the
	// same way Git's plugin does, and lore_file_reset only does that when
	// `purge` is set.
	static LoreResult discard(const std::string &p_repository_path, const std::vector<std::string> &p_paths);

	// Commits currently staged changes with `p_message`.
	static LoreResult commit(const std::string &p_repository_path, const std::string &p_message);

	// Rewrites the message of the most recent revision. Note: unlike Git's
	// `commit --amend`, this only changes the commit message — it does not
	// fold newly staged changes into the previous revision, because
	// lore_revision_amend's arguments are message-only.
	static LoreResult amend(const std::string &p_repository_path, const std::string &p_message);
};

} // namespace lore_ffi
