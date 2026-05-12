#pragma once
//
// PalRtos — task creation that lets the caller pick the core and the stack
// size. ESP32: `xTaskCreatePinnedToCore`. PC: `std::thread` (core_id is
// advisory, ignored).
//
// Why this exists in Sprint 7: Sprint 5 had to disable multi-core
// scheduling on ESP32 because arduino-esp32's `std::thread` maps to pthread
// with a ~3 KB default stack — too small for `Scheduler::core_loop`'s
// mutex + JSON dispatch — which triggered a Core 1 panic on the first tick.
// `xTaskCreatePinnedToCore` lets us pick 8 KB and choose a core; both fix
// the crash and enable the Sprint 7 effect→artnet split.
//
// Frugality: this file does not own a thread-management abstraction beyond
// "start a task that runs forever, never joined". Sprint 5's `Scheduler`
// joins its std::thread vector on PC; on ESP32 the tasks run for the
// process lifetime and are never joined. The PalRtos `task_create_pinned`
// returns nothing — there's no handle, no stop, no join surface yet.
// Sprint 8+ adds those when a caller needs them.
//

#include <cstdint>

#ifdef ARDUINO

  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>

namespace pal {

using TaskFn = void (*)(void*);

// Start a task on `core_id` (0 = PRO_CPU, 1 = APP_CPU) with `stack_bytes`
// of stack and the given priority. `arg` is passed to `fn`. The task runs
// for the process lifetime; no handle is returned (Sprint 7 callers don't
// need to stop or join).
inline bool task_create_pinned(TaskFn fn,
                               const char* name,
                               uint32_t stack_bytes,
                               void* arg,
                               uint32_t priority,
                               int core_id) {
  BaseType_t r = xTaskCreatePinnedToCore(
      fn, name, stack_bytes / sizeof(StackType_t), arg,
      priority, nullptr, (BaseType_t)core_id);
  return r == pdPASS;
}

}  // namespace pal

#else  // PC

  #include <functional>
  #include <thread>

namespace pal {

using TaskFn = void (*)(void*);

// PC: spawns a detached std::thread. core_id is ignored — the OS scheduler
// decides. Returns true if the thread was launched.
inline bool task_create_pinned(TaskFn fn,
                               const char* /*name*/,
                               uint32_t /*stack_bytes*/,
                               void* arg,
                               uint32_t /*priority*/,
                               int /*core_id*/) {
  std::thread([fn, arg]() { fn(arg); }).detach();
  return true;
}

}  // namespace pal

#endif
