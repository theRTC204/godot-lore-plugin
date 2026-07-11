// Standalone console program exercising lore_ffi::LoreClient against a real
// local Lore repository, with no Godot involved. Used to validate the FFI
// wrapper independently, and to cross-check its output against the `lore`
// CLI's own output for the same repository.
//
// Usage:
//   lore_ffi_test <path-to-lore-repository>
//   lore_ffi_test <path-to-lore-repository> --write-ops-demo <commit-message>
//   lore_ffi_test <path-to-lore-repository> --discard-demo <repository-relative-path>
//   lore_ffi_test <path-to-lore-repository> --remote-info
//   lore_ffi_test <path-to-lore-repository> --branch-demo <new-branch-name>
//   lore_ffi_test <path-to-lore-repository> --push-demo <commit-message>
//   lore_ffi_test <path-to-lore-repository> --pull-demo
//
// All demo modes except --remote-info mutate the repository (stage/unstage/
// commit, discard a file, create/checkout a branch, push, or pull) — run
// them against a disposable fixture, not anything you care about.

#include "lore_ffi/lore_client.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

const char *action_name(lore_ffi::FileAction p_action) {
	switch (p_action) {
		case lore_ffi::FileAction::Add:
			return "add";
		case lore_ffi::FileAction::Delete:
			return "delete";
		case lore_ffi::FileAction::Move:
			return "move";
		case lore_ffi::FileAction::Copy:
			return "copy";
		case lore_ffi::FileAction::Keep:
		default:
			return "keep";
	}
}

bool print_status(const std::string &p_repository_path, std::vector<lore_ffi::FileStatus> &r_files) {
	std::printf("== lore_repository_status(%s) ==\n", p_repository_path.c_str());
	lore_ffi::LoreResult status_result = lore_ffi::LoreClient::status(p_repository_path, r_files);
	if (!status_result.ok) {
		std::fprintf(stderr, "status failed: %s\n", status_result.error_message.c_str());
		return false;
	}
	for (const lore_ffi::FileStatus &file : r_files) {
		std::printf(
				"  %-8s %-40s size=%llu staged=%d dirty=%d conflict=%d\n",
				action_name(file.action),
				file.path.c_str(),
				static_cast<unsigned long long>(file.size),
				file.staged ? 1 : 0,
				file.dirty ? 1 : 0,
				file.conflict ? 1 : 0);
	}
	std::printf("  (%zu files)\n", r_files.size());
	return true;
}

bool print_diff(const std::string &p_repository_path) {
	std::printf("\n== lore_file_diff(%s) ==\n", p_repository_path.c_str());
	std::vector<lore_ffi::FileDiff> diffs;
	lore_ffi::LoreResult diff_result = lore_ffi::LoreClient::diff(p_repository_path, {}, diffs);
	if (!diff_result.ok) {
		std::fprintf(stderr, "diff failed: %s\n", diff_result.error_message.c_str());
		return false;
	}
	for (const lore_ffi::FileDiff &diff_entry : diffs) {
		std::printf("--- %s (%s) ---\n%s\n", diff_entry.path.c_str(), action_name(diff_entry.action), diff_entry.patch.c_str());
	}
	std::printf("  (%zu files with diffs)\n", diffs.size());
	return true;
}

bool print_history(const std::string &p_repository_path) {
	std::printf("\n== lore_revision_history(%s) ==\n", p_repository_path.c_str());
	std::vector<lore_ffi::RevisionHistoryEntry> entries;
	lore_ffi::LoreResult history_result = lore_ffi::LoreClient::history(p_repository_path, 0, entries);
	if (!history_result.ok) {
		std::fprintf(stderr, "history failed: %s\n", history_result.error_message.c_str());
		return false;
	}
	for (const lore_ffi::RevisionHistoryEntry &entry : entries) {
		std::printf(
				"Revision  : %llu\n"
				"Signature : %s\n"
				"Parent    : %s\n"
				"Merge     : %s\n"
				"Author    : %s\n"
				"Timestamp : %lld\n"
				"Message   : %s\n\n",
				static_cast<unsigned long long>(entry.revision_number),
				entry.revision.c_str(),
				entry.parent.c_str(),
				entry.parent_other.c_str(),
				entry.author.c_str(),
				static_cast<long long>(entry.unix_timestamp),
				entry.message.c_str());
	}
	std::printf("  (%zu revisions)\n", entries.size());
	return true;
}

int run_status_diff(const std::string &p_repository_path) {
	int exit_code = 0;
	std::vector<lore_ffi::FileStatus> files;
	if (!print_status(p_repository_path, files)) {
		exit_code = 1;
	}
	if (!print_diff(p_repository_path)) {
		exit_code = 1;
	}
	if (!print_history(p_repository_path)) {
		exit_code = 1;
	}
	return exit_code;
}

// Stages every currently-unstaged file, unstages and re-stages the first one
// (round-tripping _unstage_file), commits, then prints status again (should
// come back empty: everything just got committed).
int run_write_ops_demo(const std::string &p_repository_path, const std::string &p_commit_message) {
	std::vector<lore_ffi::FileStatus> files;
	if (!print_status(p_repository_path, files)) {
		return 1;
	}

	std::vector<std::string> unstaged_paths;
	for (const lore_ffi::FileStatus &file : files) {
		if (!file.staged) {
			unstaged_paths.push_back(file.path);
		}
	}

	if (unstaged_paths.empty()) {
		std::fprintf(stderr, "nothing unstaged to demo with\n");
		return 1;
	}

	std::printf("\n== stage(%zu files) ==\n", unstaged_paths.size());
	lore_ffi::LoreResult stage_result = lore_ffi::LoreClient::stage(p_repository_path, unstaged_paths);
	if (!stage_result.ok) {
		std::fprintf(stderr, "stage failed: %s\n", stage_result.error_message.c_str());
		return 1;
	}

	std::printf("\n== unstage(%s) ==\n", unstaged_paths.front().c_str());
	lore_ffi::LoreResult unstage_result = lore_ffi::LoreClient::unstage(p_repository_path, { unstaged_paths.front() });
	if (!unstage_result.ok) {
		std::fprintf(stderr, "unstage failed: %s\n", unstage_result.error_message.c_str());
		return 1;
	}
	print_status(p_repository_path, files);

	std::printf("\n== re-stage(%s) ==\n", unstaged_paths.front().c_str());
	stage_result = lore_ffi::LoreClient::stage(p_repository_path, { unstaged_paths.front() });
	if (!stage_result.ok) {
		std::fprintf(stderr, "re-stage failed: %s\n", stage_result.error_message.c_str());
		return 1;
	}

	std::printf("\n== commit(\"%s\") ==\n", p_commit_message.c_str());
	lore_ffi::LoreResult commit_result = lore_ffi::LoreClient::commit(p_repository_path, p_commit_message);
	if (!commit_result.ok) {
		std::fprintf(stderr, "commit failed: %s\n", commit_result.error_message.c_str());
		return 1;
	}

	std::printf("\n== status after commit (expect 0 files) ==\n");
	print_status(p_repository_path, files);

	return files.empty() ? 0 : 1;
}

// Discards `p_path`. For a never-committed file this should remove it
// entirely (LoreClient::discard always sets purge=1); verify via status
// before/after and note whether the size on disk actually shrank in the
// caller's own follow-up checks.
int run_discard_demo(const std::string &p_repository_path, const std::string &p_path) {
	std::vector<lore_ffi::FileStatus> files;
	if (!print_status(p_repository_path, files)) {
		return 1;
	}

	std::printf("\n== discard(%s) ==\n", p_path.c_str());
	lore_ffi::LoreResult discard_result = lore_ffi::LoreClient::discard(p_repository_path, { p_path });
	if (!discard_result.ok) {
		std::fprintf(stderr, "discard failed: %s\n", discard_result.error_message.c_str());
		return 1;
	}

	std::printf("\n== status after discard ==\n");
	return print_status(p_repository_path, files) ? 0 : 1;
}

int run_remote_info(const std::string &p_repository_path) {
	int exit_code = 0;

	std::vector<std::string> branches;
	lore_ffi::LoreResult branch_list_result = lore_ffi::LoreClient::branch_list(p_repository_path, branches);
	if (!branch_list_result.ok) {
		std::fprintf(stderr, "branch_list failed: %s\n", branch_list_result.error_message.c_str());
		exit_code = 1;
	} else {
		std::printf("branches:\n");
		for (const std::string &branch : branches) {
			std::printf("  %s\n", branch.c_str());
		}
	}

	std::string current_branch;
	lore_ffi::LoreResult current_result = lore_ffi::LoreClient::current_branch_name(p_repository_path, current_branch);
	if (!current_result.ok) {
		std::fprintf(stderr, "current_branch_name failed: %s\n", current_result.error_message.c_str());
		exit_code = 1;
	} else {
		std::printf("current branch: %s\n", current_branch.c_str());
	}

	std::string url;
	lore_ffi::LoreResult remote_result = lore_ffi::LoreClient::remote_url(p_repository_path, url);
	if (!remote_result.ok) {
		std::fprintf(stderr, "remote_url failed: %s\n", remote_result.error_message.c_str());
		exit_code = 1;
	} else {
		std::printf("remote url: %s\n", url.c_str());
	}

	return exit_code;
}

// Creates a new branch, checks it out, and prints branch_list/
// current_branch_name before and after to confirm both took effect.
int run_branch_demo(const std::string &p_repository_path, const std::string &p_branch_name) {
	std::printf("== before ==\n");
	if (run_remote_info(p_repository_path) != 0) {
		return 1;
	}

	std::printf("\n== create_branch(%s) ==\n", p_branch_name.c_str());
	lore_ffi::LoreResult create_result = lore_ffi::LoreClient::create_branch(p_repository_path, p_branch_name);
	if (!create_result.ok) {
		std::fprintf(stderr, "create_branch failed: %s\n", create_result.error_message.c_str());
		return 1;
	}

	std::printf("\n== checkout_branch(%s) ==\n", p_branch_name.c_str());
	lore_ffi::LoreResult checkout_result = lore_ffi::LoreClient::checkout_branch(p_repository_path, p_branch_name);
	if (!checkout_result.ok) {
		std::fprintf(stderr, "checkout_branch failed: %s\n", checkout_result.error_message.c_str());
		return 1;
	}

	std::printf("\n== after ==\n");
	return run_remote_info(p_repository_path);
}

// Stages every unstaged file, commits, and pushes the current branch.
int run_push_demo(const std::string &p_repository_path, const std::string &p_commit_message) {
	std::vector<lore_ffi::FileStatus> files;
	if (!print_status(p_repository_path, files)) {
		return 1;
	}

	std::vector<std::string> unstaged_paths;
	for (const lore_ffi::FileStatus &file : files) {
		if (!file.staged) {
			unstaged_paths.push_back(file.path);
		}
	}

	if (!unstaged_paths.empty()) {
		std::printf("\n== stage(%zu files) ==\n", unstaged_paths.size());
		lore_ffi::LoreResult stage_result = lore_ffi::LoreClient::stage(p_repository_path, unstaged_paths);
		if (!stage_result.ok) {
			std::fprintf(stderr, "stage failed: %s\n", stage_result.error_message.c_str());
			return 1;
		}

		std::printf("\n== commit(\"%s\") ==\n", p_commit_message.c_str());
		lore_ffi::LoreResult commit_result = lore_ffi::LoreClient::commit(p_repository_path, p_commit_message);
		if (!commit_result.ok) {
			std::fprintf(stderr, "commit failed: %s\n", commit_result.error_message.c_str());
			return 1;
		}
	} else {
		std::printf("\n(nothing unstaged; pushing whatever is already committed)\n");
	}

	std::string current_branch;
	lore_ffi::LoreResult current_result = lore_ffi::LoreClient::current_branch_name(p_repository_path, current_branch);
	if (!current_result.ok) {
		std::fprintf(stderr, "current_branch_name failed: %s\n", current_result.error_message.c_str());
		return 1;
	}

	std::printf("\n== push(%s) ==\n", current_branch.c_str());
	lore_ffi::LoreResult push_result = lore_ffi::LoreClient::push(p_repository_path, current_branch);
	if (!push_result.ok) {
		std::fprintf(stderr, "push failed: %s\n", push_result.error_message.c_str());
		return 1;
	}
	std::printf("push succeeded\n");
	return 0;
}

int run_pull_demo(const std::string &p_repository_path) {
	std::printf("== before ==\n");
	std::vector<lore_ffi::FileStatus> files;
	print_status(p_repository_path, files);

	std::printf("\n== pull ==\n");
	lore_ffi::LoreResult pull_result = lore_ffi::LoreClient::pull(p_repository_path);
	if (!pull_result.ok) {
		std::fprintf(stderr, "pull failed: %s\n", pull_result.error_message.c_str());
		return 1;
	}

	std::printf("\n== after ==\n");
	return print_status(p_repository_path, files) ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <path-to-lore-repository> [--write-ops-demo <message> | --discard-demo <path> | --remote-info | --branch-demo <name> | --push-demo <message> | --pull-demo]\n", argv[0]);
		return 2;
	}

	const std::string repository_path = argv[1];
	lore_ffi::LoreClient::initialize();

	int exit_code;
	if (argc >= 4 && std::string(argv[2]) == "--write-ops-demo") {
		exit_code = run_write_ops_demo(repository_path, argv[3]);
	} else if (argc >= 4 && std::string(argv[2]) == "--discard-demo") {
		exit_code = run_discard_demo(repository_path, argv[3]);
	} else if (argc >= 3 && std::string(argv[2]) == "--remote-info") {
		exit_code = run_remote_info(repository_path);
	} else if (argc >= 4 && std::string(argv[2]) == "--branch-demo") {
		exit_code = run_branch_demo(repository_path, argv[3]);
	} else if (argc >= 4 && std::string(argv[2]) == "--push-demo") {
		exit_code = run_push_demo(repository_path, argv[3]);
	} else if (argc >= 3 && std::string(argv[2]) == "--pull-demo") {
		exit_code = run_pull_demo(repository_path);
	} else {
		exit_code = run_status_diff(repository_path);
	}

	lore_ffi::LoreClient::shutdown();
	return exit_code;
}
