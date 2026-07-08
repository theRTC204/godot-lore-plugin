#include "lore_vcs_plugin.h"

#include "lore_ffi/lore_client.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace godot;

namespace {

EditorVCSInterface::ChangeType to_change_type(const lore_ffi::FileStatus &p_file) {
	if (p_file.conflict) {
		return EditorVCSInterface::CHANGE_TYPE_UNMERGED;
	}
	switch (p_file.action) {
		case lore_ffi::FileAction::Add:
			return EditorVCSInterface::CHANGE_TYPE_NEW;
		case lore_ffi::FileAction::Delete:
			return EditorVCSInterface::CHANGE_TYPE_DELETED;
		case lore_ffi::FileAction::Move:
			return EditorVCSInterface::CHANGE_TYPE_RENAMED;
		case lore_ffi::FileAction::Copy:
			// Godot's ChangeType has no Copy variant; a copy is new content
			// at a new path from the editor's point of view.
			return EditorVCSInterface::CHANGE_TYPE_NEW;
		case lore_ffi::FileAction::Keep:
		default:
			// Lore has no dedicated "Modify" action (see the note on
			// lore_ffi::FileAction): a content change to an existing file is
			// Keep + dirty, which is what CHANGE_TYPE_MODIFIED means here.
			return EditorVCSInterface::CHANGE_TYPE_MODIFIED;
	}
}

// One line of a parsed unified-diff hunk, ready for
// EditorVCSInterface::create_diff_line (new_line_no, old_line_no, content,
// status), where status is exactly "+", "-", or " " (see
// version_control_editor_plugin.cpp's diff renderer, which compares against
// those literal single-character strings).
struct ParsedDiffLine {
	int32_t old_line_no;
	int32_t new_line_no;
	std::string content;
	std::string status;
};

struct ParsedDiffHunk {
	int32_t old_start;
	int32_t new_start;
	int32_t old_lines;
	int32_t new_lines;
	std::vector<ParsedDiffLine> lines;
};

struct ParsedDiff {
	std::string old_file;
	std::string new_file;
	std::vector<ParsedDiffHunk> hunks;
};

// Parses a "@@ -oldStart[,oldLines] +newStart[,newLines] @@" unified-diff
// hunk header. Either count is omitted when it is 1, so all four
// combinations must be tried.
bool parse_hunk_header(const std::string &p_line, int32_t &r_old_start, int32_t &r_old_lines, int32_t &r_new_start, int32_t &r_new_lines) {
	r_old_lines = 1;
	r_new_lines = 1;
	if (std::sscanf(p_line.c_str(), "@@ -%d,%d +%d,%d @@", &r_old_start, &r_old_lines, &r_new_start, &r_new_lines) == 4) {
		return true;
	}
	if (std::sscanf(p_line.c_str(), "@@ -%d,%d +%d @@", &r_old_start, &r_old_lines, &r_new_start) == 3) {
		return true;
	}
	if (std::sscanf(p_line.c_str(), "@@ -%d +%d,%d @@", &r_old_start, &r_new_start, &r_new_lines) == 3) {
		return true;
	}
	if (std::sscanf(p_line.c_str(), "@@ -%d +%d @@", &r_old_start, &r_new_start) == 2) {
		return true;
	}
	return false;
}

// lore_file_diff's `patch` field is already scoped to a single file (unlike
// the `lore diff` CLI's combined multi-file text), so this only needs to
// parse one "--- .. / +++ .. / @@ .. @@ / <lines>" unified-diff block, not
// split multiple files apart.
ParsedDiff parse_unified_diff(const std::string &p_patch) {
	ParsedDiff diff;
	ParsedDiffHunk *current_hunk = nullptr;
	int32_t old_line = 0;
	int32_t new_line = 0;

	size_t pos = 0;
	while (pos <= p_patch.size()) {
		size_t newline_pos = p_patch.find('\n', pos);
		std::string line = (newline_pos == std::string::npos) ? p_patch.substr(pos) : p_patch.substr(pos, newline_pos - pos);

		if (line.compare(0, 4, "--- ") == 0) {
			diff.old_file = line.substr(4);
		} else if (line.compare(0, 4, "+++ ") == 0) {
			diff.new_file = line.substr(4);
		} else if (line.compare(0, 3, "@@ ") == 0) {
			int32_t old_start = 0, old_lines = 0, new_start = 0, new_lines = 0;
			if (parse_hunk_header(line, old_start, old_lines, new_start, new_lines)) {
				diff.hunks.push_back(ParsedDiffHunk{ old_start, new_start, old_lines, new_lines, {} });
				current_hunk = &diff.hunks.back();
				old_line = old_start;
				new_line = new_start;
			}
		} else if (current_hunk != nullptr && !line.empty() && (line[0] == ' ' || line[0] == '+' || line[0] == '-')) {
			ParsedDiffLine diff_line;
			diff_line.status = line.substr(0, 1);
			diff_line.content = line.substr(1);
			if (line[0] == ' ') {
				diff_line.old_line_no = old_line++;
				diff_line.new_line_no = new_line++;
			} else if (line[0] == '+') {
				diff_line.old_line_no = -1;
				diff_line.new_line_no = new_line++;
			} else { // '-'
				diff_line.old_line_no = old_line++;
				diff_line.new_line_no = -1;
			}
			current_hunk->lines.push_back(std::move(diff_line));
		}

		if (newline_pos == std::string::npos) {
			break;
		}
		pos = newline_pos + 1;
	}

	return diff;
}

} // namespace

void LoreVCSPlugin::_bind_methods() {
}

bool LoreVCSPlugin::_initialize(const String &p_project_path) {
	repository_path = p_project_path;
	lore_ffi::LoreClient::initialize();
	return true;
}

bool LoreVCSPlugin::_shut_down() {
	lore_ffi::LoreClient::shutdown();
	return true;
}

String LoreVCSPlugin::_get_vcs_name() {
	return "Lore";
}

void LoreVCSPlugin::_set_credentials(const String &p_username, const String &p_password, const String &p_ssh_public_key_path, const String &p_ssh_private_key_path, const String &p_ssh_passphrase) {
	// TODO: wrap lore_auth_login_with_token / lore_auth_login_interactive in
	// lore_ffi. Lore's auth model (JWT bearer tokens via an OS keyring-backed
	// login service) doesn't map directly onto Git's username/password/SSH
	// key fields anyway; this needs its own design, not just a passthrough.
}

TypedArray<Dictionary> LoreVCSPlugin::_get_previous_commits(int32_t p_max_commits) {
	// TODO: wrap lore_revision_history in lore_ffi and populate this via
	// create_commit(msg, author, id, unix_timestamp, offset_minutes).
	return TypedArray<Dictionary>();
}

void LoreVCSPlugin::_create_remote(const String &p_remote_name, const String &p_remote_url) {
	// Lore has one server per repository, configured at `lore repository
	// create`/`clone` time, not a runtime-addable list of named remotes like
	// Git's — there's no lore-capi operation this maps onto.
}

void LoreVCSPlugin::_remove_remote(const String &p_remote_name) {
	// See _create_remote.
}

void LoreVCSPlugin::_fetch(const String &p_remote) {
	// TODO: Lore has no fetch-without-integrating operation the way Git
	// does (see lore_ffi::LoreClient::pull, which is the closest
	// equivalent and always integrates). Leaving this a no-op rather than
	// silently calling pull() from here, since a "Fetch" button that
	// secretly performs a full sync would surprise anyone used to Git's
	// fetch/pull distinction.
}

TypedArray<String> LoreVCSPlugin::_get_branch_list() {
	TypedArray<String> result;

	std::vector<std::string> branches;
	lore_ffi::LoreResult branch_result = lore_ffi::LoreClient::branch_list(repository_path.utf8().get_data(), branches);
	if (!branch_result.ok) {
		popup_error(String("Lore branch list failed: ") + String(branch_result.error_message.c_str()));
		return result;
	}

	for (const std::string &branch : branches) {
		result.push_back(String(branch.c_str()));
	}
	return result;
}

String LoreVCSPlugin::_get_current_branch_name() {
	std::string branch_name;
	lore_ffi::LoreResult result = lore_ffi::LoreClient::current_branch_name(repository_path.utf8().get_data(), branch_name);
	if (!result.ok) {
		popup_error(String("Lore current branch lookup failed: ") + String(result.error_message.c_str()));
		return String();
	}
	return String(branch_name.c_str());
}

bool LoreVCSPlugin::_checkout_branch(const String &p_branch_name) {
	lore_ffi::LoreResult result = lore_ffi::LoreClient::checkout_branch(repository_path.utf8().get_data(), p_branch_name.utf8().get_data());
	if (!result.ok) {
		popup_error(String("Lore checkout failed: ") + String(result.error_message.c_str()));
		return false;
	}
	return true;
}

void LoreVCSPlugin::_create_branch(const String &p_branch_name) {
	lore_ffi::LoreResult result = lore_ffi::LoreClient::create_branch(repository_path.utf8().get_data(), p_branch_name.utf8().get_data());
	if (!result.ok) {
		popup_error(String("Lore create branch failed: ") + String(result.error_message.c_str()));
	}
}

void LoreVCSPlugin::_remove_branch(const String &p_branch_name) {
	lore_ffi::LoreResult result = lore_ffi::LoreClient::remove_branch(repository_path.utf8().get_data(), p_branch_name.utf8().get_data());
	if (!result.ok) {
		popup_error(String("Lore remove (archive) branch failed: ") + String(result.error_message.c_str()));
	}
}

TypedArray<String> LoreVCSPlugin::_get_remotes() {
	TypedArray<String> result;

	std::string url;
	lore_ffi::LoreResult remote_result = lore_ffi::LoreClient::remote_url(repository_path.utf8().get_data(), url);
	if (!remote_result.ok) {
		popup_error(String("Lore remote lookup failed: ") + String(remote_result.error_message.c_str()));
		return result;
	}
	if (!url.empty()) {
		result.push_back(String(url.c_str()));
	}
	return result;
}

void LoreVCSPlugin::_push(const String &p_remote, bool p_force) {
	// p_remote is ignored: Lore has one configured remote per repository
	// (see _get_remotes), so there's no separate named-remote to select.
	std::string current_branch;
	lore_ffi::LoreResult branch_result = lore_ffi::LoreClient::current_branch_name(repository_path.utf8().get_data(), current_branch);
	if (!branch_result.ok) {
		popup_error(String("Lore push failed: ") + String(branch_result.error_message.c_str()));
		return;
	}

	lore_ffi::LoreResult result = lore_ffi::LoreClient::push(repository_path.utf8().get_data(), current_branch);
	if (!result.ok) {
		popup_error(String("Lore push failed: ") + String(result.error_message.c_str()));
	}
}

void LoreVCSPlugin::_pull(const String &p_remote) {
	// p_remote is ignored: see _push.
	lore_ffi::LoreResult result = lore_ffi::LoreClient::pull(repository_path.utf8().get_data());
	if (!result.ok) {
		popup_error(String("Lore pull failed: ") + String(result.error_message.c_str()));
	}
}

TypedArray<Dictionary> LoreVCSPlugin::_get_modified_files_data() {
	TypedArray<Dictionary> result;

	std::vector<lore_ffi::FileStatus> files;
	lore_ffi::LoreResult status_result = lore_ffi::LoreClient::status(repository_path.utf8().get_data(), files);
	if (!status_result.ok) {
		popup_error(String("Lore status failed: ") + String(status_result.error_message.c_str()));
		return result;
	}

	for (const lore_ffi::FileStatus &file : files) {
		EditorVCSInterface::TreeArea area = file.staged ? EditorVCSInterface::TREE_AREA_STAGED : EditorVCSInterface::TREE_AREA_UNSTAGED;
		result.push_back(create_status_file(String(file.path.c_str()), to_change_type(file), area));
	}

	return result;
}

TypedArray<Dictionary> LoreVCSPlugin::_get_diff(const String &p_identifier, int32_t p_area) {
	TypedArray<Dictionary> result;

	if (p_area == EditorVCSInterface::TREE_AREA_COMMIT) {
		// Diffing a specific commit isn't wired up yet: that needs
		// lore_revision_diff, introduced alongside _get_previous_commits in
		// a later phase.
		return result;
	}

	std::vector<lore_ffi::FileDiff> diffs;
	std::vector<std::string> paths{ std::string(p_identifier.utf8().get_data()) };
	lore_ffi::LoreResult diff_result = lore_ffi::LoreClient::diff(repository_path.utf8().get_data(), paths, diffs);
	if (!diff_result.ok) {
		popup_error(String("Lore diff failed: ") + String(diff_result.error_message.c_str()));
		return result;
	}

	for (const lore_ffi::FileDiff &file_diff : diffs) {
		ParsedDiff parsed = parse_unified_diff(file_diff.patch);

		Dictionary diff_file = create_diff_file(
				String((parsed.new_file.empty() ? file_diff.path : parsed.new_file).c_str()),
				String((parsed.old_file.empty() ? file_diff.path : parsed.old_file).c_str()));

		TypedArray<Dictionary> hunks;
		for (const ParsedDiffHunk &hunk : parsed.hunks) {
			Dictionary hunk_dict = create_diff_hunk(hunk.old_start, hunk.new_start, hunk.old_lines, hunk.new_lines);

			TypedArray<Dictionary> lines;
			for (const ParsedDiffLine &line : hunk.lines) {
				lines.push_back(create_diff_line(line.new_line_no, line.old_line_no, String(line.content.c_str()), String(line.status.c_str())));
			}
			hunk_dict = add_line_diffs_into_diff_hunk(hunk_dict, lines);
			hunks.push_back(hunk_dict);
		}
		diff_file = add_diff_hunks_into_diff_file(diff_file, hunks);
		result.push_back(diff_file);
	}

	return result;
}

void LoreVCSPlugin::_stage_file(const String &p_file_path) {
	std::vector<std::string> paths{ std::string(p_file_path.utf8().get_data()) };
	lore_ffi::LoreResult result = lore_ffi::LoreClient::stage(repository_path.utf8().get_data(), paths);
	if (!result.ok) {
		popup_error(String("Lore stage failed: ") + String(result.error_message.c_str()));
	}
}

void LoreVCSPlugin::_unstage_file(const String &p_file_path) {
	std::vector<std::string> paths{ std::string(p_file_path.utf8().get_data()) };
	lore_ffi::LoreResult result = lore_ffi::LoreClient::unstage(repository_path.utf8().get_data(), paths);
	if (!result.ok) {
		popup_error(String("Lore unstage failed: ") + String(result.error_message.c_str()));
	}
}

void LoreVCSPlugin::_discard_file(const String &p_file_path) {
	std::vector<std::string> paths{ std::string(p_file_path.utf8().get_data()) };
	lore_ffi::LoreResult result = lore_ffi::LoreClient::discard(repository_path.utf8().get_data(), paths);
	if (!result.ok) {
		popup_error(String("Lore discard failed: ") + String(result.error_message.c_str()));
	}
}

void LoreVCSPlugin::_commit(const String &p_msg) {
	std::string message = p_msg.utf8().get_data();
	std::string repo_path = repository_path.utf8().get_data();

	lore_ffi::LoreResult result = lore_ffi::LoreClient::commit(repo_path, message);
	if (!result.ok) {
		popup_error(String("Lore commit failed: ") + String(result.error_message.c_str()));
	}
}
