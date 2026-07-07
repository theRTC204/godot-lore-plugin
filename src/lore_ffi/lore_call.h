#pragma once

#include "lore_c_api.h"

#include <windows.h>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>

namespace lore_ffi {

// Terminal result of a blocking Lore call, captured from its
// LORE_EVENT_COMPLETE event (see lore_complete_event_data_t).
struct LoreCallResult {
	int32_t status = 0;
	std::string error_message;

	bool ok() const { return status == 0; }
};

// lore-capi resolves any user-supplied path (in a `paths` args field, always
// meant to be repository-relative) against the process's *current working
// directory*, not `lore_global_args_t.repository_path` — verified
// empirically: `lore stage <relative-path>` only works when the calling
// process's CWD is the repository root; run from anywhere else, the path
// can't be resolved (LORE_EVENT_PATH_IGNORE fires instead of the operation
// actually happening) with no error surfaced through LORE_EVENT_COMPLETE's
// status. A GDExtension running inside the Godot editor has no guarantee its
// CWD is the project root, so every call temporarily chdirs there for its
// duration (see LoreCall::invoke).
//
// Not thread-safe against concurrent LoreCall::invoke calls from different
// threads (CWD is process-global) — acceptable for now since Godot only
// calls into EditorVCSInterface from a single thread at a time.
class ScopedWorkingDirectory {
public:
	explicit ScopedWorkingDirectory(const std::string &p_directory) {
		DWORD length = GetCurrentDirectoryW(0, nullptr);
		if (length > 0) {
			previous_directory.resize(length);
			GetCurrentDirectoryW(length, previous_directory.data());
			previous_directory.resize(length - 1); // GetCurrentDirectoryW counts the trailing NUL
		}

		if (p_directory.empty()) {
			return;
		}
		int wide_length = MultiByteToWideChar(CP_UTF8, 0, p_directory.c_str(), -1, nullptr, 0);
		if (wide_length <= 0) {
			return;
		}
		std::wstring wide_directory(static_cast<size_t>(wide_length), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, p_directory.c_str(), -1, wide_directory.data(), wide_length);
		SetCurrentDirectoryW(wide_directory.c_str());
	}

	~ScopedWorkingDirectory() {
		if (!previous_directory.empty()) {
			SetCurrentDirectoryW(previous_directory.c_str());
		}
	}

	ScopedWorkingDirectory(const ScopedWorkingDirectory &) = delete;
	ScopedWorkingDirectory &operator=(const ScopedWorkingDirectory &) = delete;

private:
	std::wstring previous_directory;
};

// Runs a Lore C API operation (e.g. lore_repository_status, lore_file_diff)
// synchronously from the caller's point of view.
//
// Lore's C API is callback/event-driven: the operation function itself
// returns almost immediately (0 just means "the call was accepted", not "the
// operation is done"), and the library streams lore_event_t values back on a
// worker thread it manages, ending with a LORE_EVENT_END event that is
// always the last one delivered. Godot's EditorVCSInterface virtual methods,
// by contrast, must return synchronously with a final result — this adapter
// bridges the two by blocking the calling thread until LORE_EVENT_END
// arrives.
//
// `on_event` is invoked once per event, synchronously, on the library's
// worker thread. The same lifetime rules as the raw C callback apply: the
// event (and any lore_string_t data it points to) is only valid for the
// duration of that one invocation, so copy out anything the caller needs to
// keep. Avoid slow work in `on_event` — it blocks the library's other work
// and can stall other in-flight calls.
class LoreCall {
public:
	using EventSink = std::function<void(const lore_event_t &)>;

	template <typename ArgsT>
	static LoreCallResult invoke(
			int32_t (*p_operation)(const lore_global_args_t *, const ArgsT *, lore_event_callback_config_t),
			const lore_global_args_t &p_globals,
			const ArgsT &p_args,
			const EventSink &p_on_event) {
		ScopedWorkingDirectory scoped_cwd(std::string(p_globals.repository_path.string, p_globals.repository_path.length));

		State state{ p_on_event };

		lore_event_callback_config_t callback{};
		callback.user_context = reinterpret_cast<uint64_t>(&state);
		callback.func = &LoreCall::trampoline;

		int32_t accepted = p_operation(&p_globals, &p_args, callback);
		if (accepted != 0) {
			LoreCallResult result;
			result.status = accepted;
			result.error_message = "lore call was rejected before it started";
			return result;
		}

		std::unique_lock<std::mutex> lock(state.mutex);
		state.done_cv.wait(lock, [&state] { return state.done; });

		return state.result;
	}

private:
	struct State {
		const EventSink &on_event;
		std::mutex mutex;
		std::condition_variable done_cv;
		bool done = false;
		LoreCallResult result;
	};

	static void trampoline(const lore_event_t *p_event, uint64_t p_user_context) {
		State *state = reinterpret_cast<State *>(p_user_context);

		state->on_event(*p_event);

		if (p_event->tag == LORE_EVENT_COMPLETE) {
			state->result.status = p_event->complete.status;
			if (p_event->complete.error.message.length > 0) {
				state->result.error_message.assign(p_event->complete.error.message.string, p_event->complete.error.message.length);
			}
		} else if (p_event->tag == LORE_EVENT_END) {
			std::lock_guard<std::mutex> lock(state->mutex);
			state->done = true;
			state->done_cv.notify_one();
		}
	}
};

} // namespace lore_ffi
