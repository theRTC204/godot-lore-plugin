#pragma once

// third_party/lore/include/lore.h is generated with cbindgen's `language =
// "C"` and has no `__cplusplus` guard, so it never adds `extern "C"` itself.
// The symbols it declares are exported from lore.dll as plain, unmangled C
// symbols (Rust's `#[no_mangle] extern "C" fn`), so every include of this
// header must be wrapped in `extern "C"` here, or the linker will look for
// C++-mangled names that do not exist in the DLL.
extern "C" {
#include <lore.h>
}
