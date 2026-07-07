#pragma once

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
// actually enforced: the dock's connect flow unconditionally calls
// set_credentials (with the setup dialog's fields, even if left blank) and
// get_previous_commits/get_branch_list/get_current_branch_name/get_remotes
// (not only when their panel is opened) as soon as a plugin connects, and an
// unoverridden _REQUIRED virtual logs
// "Required virtual method ... must be overridden before calling" every time
// it's invoked. So those are all overridden here too, as honest no-op/empty
// stubs, purely to satisfy that contract — their real implementations land
// with auth, branch/remote support, and commit history in a later phase.
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

	// Stubs: see class comment above. Not implemented yet.
	virtual void _set_credentials(const String &p_username, const String &p_password, const String &p_ssh_public_key_path, const String &p_ssh_private_key_path, const String &p_ssh_passphrase) override;
	virtual TypedArray<Dictionary> _get_previous_commits(int32_t p_max_commits) override;
	virtual TypedArray<String> _get_branch_list() override;
	virtual String _get_current_branch_name() override;
	virtual TypedArray<String> _get_remotes() override;

private:
	// The directory containing the repository's .lore folder. Assumed to be
	// exactly the Godot project root (what the editor passes to
	// _initialize) for now; Lore does not walk upward looking for .lore the
	// way Git walks looking for .git, so a project with .lore in an
	// ancestor directory isn't supported yet.
	String repository_path;
};

} // namespace godot
