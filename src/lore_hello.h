#pragma once

#include <godot_cpp/classes/ref_counted.hpp>

namespace godot {

// Phase 1 placeholder: proves the CMake -> godot-cpp -> .gdextension -> editor
// pipeline works before any Lore or EditorVCSInterface wiring is introduced.
class LoreHello : public RefCounted {
	GDCLASS(LoreHello, RefCounted)

protected:
	static void _bind_methods();

public:
	String greet() const;
};

} // namespace godot
