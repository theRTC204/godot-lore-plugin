#pragma once

#include "lore_ffi/lore_client.h"

#include <godot_cpp/classes/editor_vcs_interface.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace godot {

// GDExtension implementation of Godot's VCS plugin interface for the Lore
// source control system, mirroring godot-git-plugin's role for Git. Real
// functionality so far: status, working-tree diff, stage/unstage/discard,
// commit, and branch list/current/checkout/create/remove (archive) plus
// push/pull against the repository's single configured remote.
//
// Targets the Godot 4.3 GDExtension API (see GODOTCPP_API_VERSION in the
// top-level CMakeLists.txt) to keep compatibility_minimum as low as
// possible, which is why EditorVCSInterface's amend support isn't wired up:
// _allow_amends() and _commit's `amend` parameter were both added in Godot
// 4.7, so the underlying lore_ffi::LoreClient::amend is currently unused
// GDExtension-side. Revisit if this plugin ever ships a 4.7+-only build.
//
// EditorVCSInterface's "_REQUIRED" virtuals (see editor_vcs_interface.h) are
// actually enforced at call time, not just a doc annotation: an unoverridden
// _REQUIRED virtual logs
// "Required virtual method ... must be overridden before calling" every time
// it's invoked, including several the dock's connect flow calls
// unconditionally (set_credentials, get_previous_commits) regardless of
// which panel is open. The remaining stubs below (auth, commit history,
// remotes-as-a-concept, fetch) are honest no-ops for operations Lore either
// doesn't support the way Git does (no multiple named remotes, no
// fetch-without-integrating) or that need more design/infra than this phase
// covers (auth, commit history) — not gaps in what's wired up so far.
class LoreVCSPlugin : public EditorVCSInterface {
	GDCLASS(LoreVCSPlugin, EditorVCSInterface)

protected:
	static void _bind_methods();

public:
	virtual bool _initialize(const String &p_project_path) override;
	virtual bool _shut_down() override;
	virtual String _get_vcs_name() override;
	virtual TypedArray<Dictionary> _get_modified_files_data() override;
	virtual TypedArray<Dictionary> _get_diff(const String &p_identifier, int32_t p_area) override;
	virtual void _stage_file(const String &p_file_path) override;
	virtual void _unstage_file(const String &p_file_path) override;
	virtual void _discard_file(const String &p_file_path) override;
	virtual void _commit(const String &p_msg) override;
	virtual TypedArray<String> _get_branch_list() override;
	virtual String _get_current_branch_name() override;
	virtual bool _checkout_branch(const String &p_branch_name) override;
	virtual void _create_branch(const String &p_branch_name) override;
	virtual void _remove_branch(const String &p_branch_name) override;
	virtual TypedArray<String> _get_remotes() override;
	virtual void _push(const String &p_remote, bool p_force) override;
	virtual void _pull(const String &p_remote) override;

	// Stubs: see class comment above. Not implemented yet.
	virtual void _set_credentials(const String &p_username, const String &p_password, const String &p_ssh_public_key_path, const String &p_ssh_private_key_path, const String &p_ssh_passphrase) override;
	virtual TypedArray<Dictionary> _get_previous_commits(int32_t p_max_commits) override;
	virtual void _create_remote(const String &p_remote_name, const String &p_remote_url) override;
	virtual void _remove_remote(const String &p_remote_name) override;
	virtual void _fetch(const String &p_remote) override;

private:
	// Reports a failed lore_ffi call to the user via popup_error, as
	// "<p_action>: <message>". Falls back to the raw status code when Lore
	// didn't populate an error message (it doesn't always, even for a
	// failing call) so the popup is never just "<p_action>: " with nothing
	// after it.
	void report_error(const String &p_action, const lore_ffi::LoreResult &p_result);

	// Writes a default .loreignore at the project root if one doesn't
	// already exist. Mirrors exactly what Godot's own built-in Git
	// integration writes (EditorVCSInterface::create_vcs_metadata_files) —
	// see the .cpp for why. No dialog: the new file just shows up as an
	// untracked file on the next status refresh, same as any other new file.
	void ensure_loreignore_exists();

	// The directory containing the repository's .lore folder. Assumed to be
	// exactly the Godot project root (what the editor passes to
	// _initialize) for now; Lore does not walk upward looking for .lore the
	// way Git walks looking for .git, so a project with .lore in an
	// ancestor directory isn't supported yet.
	String repository_path;
};

} // namespace godot
