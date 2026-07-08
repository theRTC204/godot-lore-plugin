#include "lore_client.h"

#include "lore_call.h"

namespace lore_ffi {

namespace {

lore_string_t to_lore_string(const std::string &p_str) {
	return lore_string_t{ p_str.data(), p_str.size() };
}

std::string from_lore_string(const lore_string_t &p_str) {
	if (p_str.length == 0) {
		return std::string();
	}
	return std::string(p_str.string, p_str.length);
}

FileAction to_file_action(lore_file_action_t p_action) {
	switch (p_action) {
		case LORE_FILE_ACTION_ADD:
			return FileAction::Add;
		case LORE_FILE_ACTION_DELETE:
			return FileAction::Delete;
		case LORE_FILE_ACTION_MOVE:
			return FileAction::Move;
		case LORE_FILE_ACTION_COPY:
			return FileAction::Copy;
		case LORE_FILE_ACTION_KEEP:
		default:
			return FileAction::Keep;
	}
}

lore_global_args_t make_global_args(const std::string &p_repository_path) {
	lore_global_args_t globals{};
	globals.repository_path = to_lore_string(p_repository_path);
	return globals;
}

std::vector<lore_string_t> to_path_array(const std::vector<std::string> &p_paths) {
	std::vector<lore_string_t> path_strings;
	path_strings.reserve(p_paths.size());
	for (const std::string &path : p_paths) {
		path_strings.push_back(to_lore_string(path));
	}
	return path_strings;
}

LoreResult to_lore_result(const LoreCallResult &p_call_result) {
	LoreResult result;
	result.ok = p_call_result.ok();
	result.status = p_call_result.status;
	result.error_message = p_call_result.error_message;
	return result;
}

void ignore_events(const lore_event_t &) {
}

} // namespace

void LoreClient::initialize() {
	// lore-capi has no required init step (logging config and the thread
	// limit are both explicitly optional, callable at any time); this exists
	// as a stable hook for later phases (e.g. wiring lore_log_configure to
	// Godot's own logging) rather than because Lore requires it today.
}

void LoreClient::shutdown() {
	lore_shutdown();
}

LoreResult LoreClient::status(const std::string &p_repository_path, std::vector<FileStatus> &r_files) {
	r_files.clear();

	lore_global_args_t globals = make_global_args(p_repository_path);
	lore_repository_status_args_t args{};
	// Despite its one-line doc comment ("Include staged state in the
	// report"), this gates emission of LORE_EVENT_REPOSITORY_STATUS_FILE
	// entirely: with it left at 0, no per-file events are emitted at all
	// (verified empirically against a fixture repo with staged, dirty, and
	// untracked files), not just staged ones. The `lore` CLI's own plain
	// `status` (no flags) always sets it, so match that here.
	args.staged = 1;
	// Without `scan`, status only reports files Lore already knows are
	// dirty (from a prior `lore dirty`/stage/scan); a file the editor's
	// FileSystem dock just created or deleted on disk, that Lore has never
	// been told about, is invisible until something walks the filesystem.
	//
	// A reactive fast path (feeding EditorFileSystem's own change signals
	// into lore_file_dirty instead of rescanning) was investigated and
	// rejected: Godot's EditorFileSystem only fires path-specific signals
	// (resources_reimporting/resources_reimported/resources_reload) for
	// files that go through its *import* pipeline. A plain text file, or
	// any type Godot doesn't import, changes with no signal at all —
	// `filesystem_changed` fires but carries no path information. Driving
	// dirty-tracking off that coverage would silently stop detecting
	// changes to whole categories of files, which is a worse bug than the
	// full scan's cost. A full scan on every status refresh remains the
	// correct choice.
	args.scan = 1;

	LoreCallResult call_result = LoreCall::invoke<lore_repository_status_args_t>(
			&lore_repository_status,
			globals,
			args,
			[&r_files](const lore_event_t &p_event) {
				if (p_event.tag != LORE_EVENT_REPOSITORY_STATUS_FILE) {
					return;
				}
				const lore_repository_status_file_event_data_t &data = p_event.repository_status_file;

				FileStatus file;
				file.path = from_lore_string(data.path);
				file.from_path = from_lore_string(data.from_path);
				file.size = data.size;
				file.action = to_file_action(data.action);
				file.staged = data.flag_staged != 0;
				file.merged = data.flag_merged != 0;
				file.conflict = data.flag_conflict != 0;
				file.conflict_unresolved = data.flag_conflict_unresolved != 0;
				file.conflict_automerged = data.flag_conflict_automerged != 0;
				file.conflict_mine = data.flag_conflict_mine != 0;
				file.conflict_theirs = data.flag_conflict_theirs != 0;
				file.dirty = data.flag_dirty != 0;
				r_files.push_back(std::move(file));
			});

	return to_lore_result(call_result);
}

LoreResult LoreClient::diff(const std::string &p_repository_path, const std::vector<std::string> &p_paths, std::vector<FileDiff> &r_diffs) {
	r_diffs.clear();

	lore_global_args_t globals = make_global_args(p_repository_path);

	std::vector<lore_string_t> path_strings = to_path_array(p_paths);

	lore_file_diff_args_t args{};
	args.paths = lore_string_array_t{ path_strings.data(), path_strings.size() };
	args.context_lines = 3;

	LoreCallResult call_result = LoreCall::invoke<lore_file_diff_args_t>(
			&lore_file_diff,
			globals,
			args,
			[&r_diffs](const lore_event_t &p_event) {
				if (p_event.tag != LORE_EVENT_FILE_DIFF) {
					return;
				}
				const lore_file_diff_event_data_t &data = p_event.file_diff;

				FileDiff diff_entry;
				diff_entry.path = from_lore_string(data.path);
				diff_entry.patch = from_lore_string(data.patch);
				diff_entry.action = to_file_action(data.action);
				r_diffs.push_back(std::move(diff_entry));
			});

	return to_lore_result(call_result);
}

LoreResult LoreClient::stage(const std::string &p_repository_path, const std::vector<std::string> &p_paths) {
	lore_global_args_t globals = make_global_args(p_repository_path);

	std::vector<lore_string_t> path_strings = to_path_array(p_paths);

	lore_file_stage_args_t args{};
	args.paths = lore_string_array_t{ path_strings.data(), path_strings.size() };

	return to_lore_result(LoreCall::invoke<lore_file_stage_args_t>(&lore_file_stage, globals, args, &ignore_events));
}

LoreResult LoreClient::unstage(const std::string &p_repository_path, const std::vector<std::string> &p_paths) {
	lore_global_args_t globals = make_global_args(p_repository_path);

	std::vector<lore_string_t> path_strings = to_path_array(p_paths);

	lore_file_unstage_args_t args{};
	args.paths = lore_string_array_t{ path_strings.data(), path_strings.size() };

	return to_lore_result(LoreCall::invoke<lore_file_unstage_args_t>(&lore_file_unstage, globals, args, &ignore_events));
}

LoreResult LoreClient::discard(const std::string &p_repository_path, const std::vector<std::string> &p_paths) {
	lore_global_args_t globals = make_global_args(p_repository_path);

	std::vector<lore_string_t> path_strings = to_path_array(p_paths);

	lore_file_reset_args_t args{};
	args.paths = lore_string_array_t{ path_strings.data(), path_strings.size() };
	args.purge = 1;

	return to_lore_result(LoreCall::invoke<lore_file_reset_args_t>(&lore_file_reset, globals, args, &ignore_events));
}

LoreResult LoreClient::commit(const std::string &p_repository_path, const std::string &p_message) {
	lore_global_args_t globals = make_global_args(p_repository_path);

	lore_revision_commit_args_t args{};
	args.message = to_lore_string(p_message);

	return to_lore_result(LoreCall::invoke<lore_revision_commit_args_t>(&lore_revision_commit, globals, args, &ignore_events));
}

LoreResult LoreClient::amend(const std::string &p_repository_path, const std::string &p_message) {
	lore_global_args_t globals = make_global_args(p_repository_path);

	lore_revision_amend_args_t args{};
	args.message = to_lore_string(p_message);

	return to_lore_result(LoreCall::invoke<lore_revision_amend_args_t>(&lore_revision_amend, globals, args, &ignore_events));
}

LoreResult LoreClient::branch_list(const std::string &p_repository_path, std::vector<std::string> &r_branches) {
	r_branches.clear();

	lore_global_args_t globals = make_global_args(p_repository_path);
	lore_branch_list_args_t args{};

	LoreCallResult call_result = LoreCall::invoke<lore_branch_list_args_t>(
			&lore_branch_list,
			globals,
			args,
			[&r_branches](const lore_event_t &p_event) {
				if (p_event.tag != LORE_EVENT_BRANCH_LIST_ENTRY) {
					return;
				}
				// A repository connected to a remote reports every branch
				// once per location (local and remote both, even when it's
				// the same branch known in both places — confirmed against
				// the `lore branch list` CLI's own "Local branches"/"Remote
				// branches" sections). Godot's branch dropdown just wants
				// names to check out, so only list the ones actually
				// present locally.
				if (p_event.branch_list_entry.location != LORE_BRANCH_LOCATION_LOCAL) {
					return;
				}
				r_branches.push_back(from_lore_string(p_event.branch_list_entry.name));
			});

	return to_lore_result(call_result);
}

LoreResult LoreClient::current_branch_name(const std::string &p_repository_path, std::string &r_branch_name) {
	r_branch_name.clear();

	lore_global_args_t globals = make_global_args(p_repository_path);
	lore_repository_status_args_t args{};
	args.revision_only = 1;

	LoreCallResult call_result = LoreCall::invoke<lore_repository_status_args_t>(
			&lore_repository_status,
			globals,
			args,
			[&r_branch_name](const lore_event_t &p_event) {
				if (p_event.tag != LORE_EVENT_REPOSITORY_STATUS_REVISION) {
					return;
				}
				r_branch_name = from_lore_string(p_event.repository_status_revision.branch_name);
			});

	return to_lore_result(call_result);
}

LoreResult LoreClient::checkout_branch(const std::string &p_repository_path, const std::string &p_branch_name) {
	lore_global_args_t globals = make_global_args(p_repository_path);

	lore_branch_switch_args_t args{};
	args.branch = to_lore_string(p_branch_name);

	return to_lore_result(LoreCall::invoke<lore_branch_switch_args_t>(&lore_branch_switch, globals, args, &ignore_events));
}

LoreResult LoreClient::create_branch(const std::string &p_repository_path, const std::string &p_branch_name) {
	lore_global_args_t globals = make_global_args(p_repository_path);

	lore_branch_create_args_t args{};
	args.branch = to_lore_string(p_branch_name);

	return to_lore_result(LoreCall::invoke<lore_branch_create_args_t>(&lore_branch_create, globals, args, &ignore_events));
}

LoreResult LoreClient::remove_branch(const std::string &p_repository_path, const std::string &p_branch_name) {
	lore_global_args_t globals = make_global_args(p_repository_path);

	lore_branch_archive_args_t args{};
	args.branch = to_lore_string(p_branch_name);

	return to_lore_result(LoreCall::invoke<lore_branch_archive_args_t>(&lore_branch_archive, globals, args, &ignore_events));
}

LoreResult LoreClient::remote_url(const std::string &p_repository_path, std::string &r_remote_url) {
	r_remote_url.clear();

	lore_global_args_t globals = make_global_args(p_repository_path);
	// `key` must be a named local, not a temporary: to_lore_string returns a
	// pointer into its argument's own buffer, and a temporary's buffer is
	// freed at the end of this statement, before the call ever reads it
	// (verified empirically: using a temporary here made
	// lore_repository_config_get reject the call outright).
	static const std::string key_remote_url = "remote_url";
	lore_repository_config_get_args_t args{};
	args.key = to_lore_string(key_remote_url);

	LoreCallResult call_result = LoreCall::invoke<lore_repository_config_get_args_t>(
			&lore_repository_config_get,
			globals,
			args,
			[&r_remote_url](const lore_event_t &p_event) {
				if (p_event.tag != LORE_EVENT_REPOSITORY_CONFIG_GET) {
					return;
				}
				r_remote_url = from_lore_string(p_event.repository_config_get.value);
			});

	return to_lore_result(call_result);
}

LoreResult LoreClient::push(const std::string &p_repository_path, const std::string &p_branch_name) {
	lore_global_args_t globals = make_global_args(p_repository_path);

	lore_branch_push_args_t args{};
	args.branch = to_lore_string(p_branch_name);

	return to_lore_result(LoreCall::invoke<lore_branch_push_args_t>(&lore_branch_push, globals, args, &ignore_events));
}

LoreResult LoreClient::pull(const std::string &p_repository_path) {
	lore_global_args_t globals = make_global_args(p_repository_path);

	lore_revision_sync_args_t args{};

	return to_lore_result(LoreCall::invoke<lore_revision_sync_args_t>(&lore_revision_sync, globals, args, &ignore_events));
}

} // namespace lore_ffi
