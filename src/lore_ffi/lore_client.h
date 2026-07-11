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

// One revision in a `lore_revision_history` listing, as reported by
// LORE_EVENT_REVISION_HISTORY_ENTRY plus the LORE_EVENT_METADATA entries
// that immediately follow it (see lore-revision's history() in
// lore-revision/src/revision/history.rs: every entry event is followed by
// the revision's full metadata bag, unconditionally — the well-known keys
// "message", "timestamp", and "created-by"/"committed-by" are Lore's
// equivalents of a commit's message, date, and author). Not documented in
// lore.h's per-function event table, but verified against the `lore`
// crate's own CLI, which relies on exactly this to print `lore history`.
struct RevisionHistoryEntry {
	std::string revision; // lowercase hex-encoded hash signature
	uint64_t revision_number = 0;
	std::string parent; // lowercase hex-encoded first parent hash; empty when there is none
	std::string parent_other; // lowercase hex-encoded second (merge) parent hash; empty when there is none
	std::string message;
	std::string author; // "created-by" metadata, falling back to "committed-by"
	int64_t unix_timestamp = 0; // seconds; converted from the "timestamp" metadata's milliseconds
};

// Reports success/failure and, on failure, the message and status code from
// Lore's own error reporting (see lore_error_detail_t /
// lore_complete_event_data_t). `error_message` can be empty even when `ok`
// is false — Lore doesn't always populate a message alongside a failing
// status — so callers displaying this to a user should fall back to
// `status` rather than show an empty string.
struct LoreResult {
	bool ok = true;
	int32_t status = 0;
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

	// Lists the revision history of the current branch, most recent first.
	// `p_max_commits` caps the number of entries; 0 defers to Lore's own
	// default page size (100 as of this writing — see history_local in the
	// `lore` crate).
	static LoreResult history(const std::string &p_repository_path, uint32_t p_max_commits, std::vector<RevisionHistoryEntry> &r_entries);

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

	// Lists all (non-archived) local branch names.
	static LoreResult branch_list(const std::string &p_repository_path, std::vector<std::string> &r_branches);

	// Reports the name of the currently checked-out branch. Cheap: uses
	// lore_repository_status with revision_only set, so it costs a revision
	// lookup, not a file-status scan.
	static LoreResult current_branch_name(const std::string &p_repository_path, std::string &r_branch_name);

	// Switches the working tree to `p_branch_name`.
	static LoreResult checkout_branch(const std::string &p_repository_path, const std::string &p_branch_name);

	// Creates a new branch named `p_branch_name` at the current revision.
	static LoreResult create_branch(const std::string &p_repository_path, const std::string &p_branch_name);

	// Archives (Lore has no true delete) the branch named `p_branch_name`.
	static LoreResult remove_branch(const std::string &p_repository_path, const std::string &p_branch_name);

	// Reads the repository's configured remote URL. Lore has one server per
	// repository, not Git's multiple-named-remotes model, so this is the
	// entire "remote list."
	static LoreResult remote_url(const std::string &p_repository_path, std::string &r_remote_url);

	// Pushes the current branch to the configured remote.
	static LoreResult push(const std::string &p_repository_path, const std::string &p_branch_name);

	// Syncs the working tree to the current branch's remote tip (Git's
	// "pull": fetch + integrate, in one step — Lore has no separate
	// fetch-without-integrating operation).
	static LoreResult pull(const std::string &p_repository_path);
};

} // namespace lore_ffi
