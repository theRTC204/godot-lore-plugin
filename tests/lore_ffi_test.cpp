// Standalone console program exercising lore_ffi::LoreClient against a real
// local Lore repository, with no Godot involved. Used to validate the FFI
// wrapper independently, and to cross-check its output against the `lore`
// CLI's own output for the same repository.
//
// Usage:
//   lore_ffi_test <path-to-lore-repository>
//   lore_ffi_test <path-to-lore-repository> --write-ops-demo <commit-message>
//   lore_ffi_test <path-to-lore-repository> --discard-demo <repository-relative-path>
//
// The two demo modes mutate the repository (stage/unstage/commit, or
// discard a file entirely) — run them against a disposable fixture, not
// anything you care about.

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

int run_status_diff(const std::string &p_repository_path) {
	int exit_code = 0;
	std::vector<lore_ffi::FileStatus> files;
	if (!print_status(p_repository_path, files)) {
		exit_code = 1;
	}
	if (!print_diff(p_repository_path)) {
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

} // namespace

int main(int argc, char **argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <path-to-lore-repository> [--write-ops-demo <message> | --discard-demo <path>]\n", argv[0]);
		return 2;
	}

	const std::string repository_path = argv[1];
	lore_ffi::LoreClient::initialize();

	int exit_code;
	if (argc >= 4 && std::string(argv[2]) == "--write-ops-demo") {
		exit_code = run_write_ops_demo(repository_path, argv[3]);
	} else if (argc >= 4 && std::string(argv[2]) == "--discard-demo") {
		exit_code = run_discard_demo(repository_path, argv[3]);
	} else {
		exit_code = run_status_diff(repository_path);
	}

	lore_ffi::LoreClient::shutdown();
	return exit_code;
}
