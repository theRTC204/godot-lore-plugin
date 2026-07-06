// Standalone console program exercising lore_ffi::LoreClient against a real
// local Lore repository, with no Godot involved. Used to validate the FFI
// wrapper (Phase 2) independently, and to cross-check its output against the
// `lore` CLI's own output for the same repository.
//
// Usage: lore_ffi_test <path-to-lore-repository>

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

} // namespace

int main(int argc, char **argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <path-to-lore-repository>\n", argv[0]);
		return 2;
	}

	const std::string repository_path = argv[1];

	lore_ffi::LoreClient::initialize();

	int exit_code = 0;

	std::printf("== lore_repository_status(%s) ==\n", repository_path.c_str());
	std::vector<lore_ffi::FileStatus> files;
	lore_ffi::LoreResult status_result = lore_ffi::LoreClient::status(repository_path, files);
	if (!status_result.ok) {
		std::fprintf(stderr, "status failed: %s\n", status_result.error_message.c_str());
		exit_code = 1;
	} else {
		for (const lore_ffi::FileStatus &file : files) {
			std::printf(
					"  %-8s %-40s size=%llu staged=%d dirty=%d conflict=%d\n",
					action_name(file.action),
					file.path.c_str(),
					static_cast<unsigned long long>(file.size),
					file.staged ? 1 : 0,
					file.dirty ? 1 : 0,
					file.conflict ? 1 : 0);
		}
		std::printf("  (%zu files)\n", files.size());
	}

	std::printf("\n== lore_file_diff(%s) ==\n", repository_path.c_str());
	std::vector<lore_ffi::FileDiff> diffs;
	lore_ffi::LoreResult diff_result = lore_ffi::LoreClient::diff(repository_path, {}, diffs);
	if (!diff_result.ok) {
		std::fprintf(stderr, "diff failed: %s\n", diff_result.error_message.c_str());
		exit_code = 1;
	} else {
		for (const lore_ffi::FileDiff &diff_entry : diffs) {
			std::printf("--- %s (%s) ---\n%s\n", diff_entry.path.c_str(), action_name(diff_entry.action), diff_entry.patch.c_str());
		}
		std::printf("  (%zu files with diffs)\n", diffs.size());
	}

	lore_ffi::LoreClient::shutdown();

	return exit_code;
}
