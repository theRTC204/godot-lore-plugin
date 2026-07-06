#include "lore_hello.h"

using namespace godot;

void LoreHello::_bind_methods() {
	ClassDB::bind_method(D_METHOD("greet"), &LoreHello::greet);
}

String LoreHello::greet() const {
	return "godot-lore-plugin scaffolding is wired up.";
}
