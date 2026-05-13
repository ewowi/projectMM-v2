# StateStoreModule

Persists the module configuration + per-control values across reboots. On boot, reads `/modules.json` (LittleFS) and re-instantiates each listed module, then per-module `/state-<id>.json` applies the saved control values via `setControl`. Every 10 s, snapshots the current state and writes any diff. Sits at module slot 5 in `main.cpp` (after the four head modules) so its setup can `mm.add()` user-added modules before any other module's first loop tick. See [Sprint 6](../../development/release-01.md#sprint-6) for the design discussion.

## End-user reference

No user-facing controls beyond `enabled`. Side effects on disk:

| File | Written when | Contents |
|---|---|---|
| `/modules.json` | Module list changes (add / remove / reorder) | `[{type, id}, …]` |
| `/state-<id>.json` | Any control value on `<id>` changes | `{control_key: value, …}` per module |

Both files are gitignored on PC (PalFs PC root is `./state/`).

## Developer reference

- `setup()` — read `/modules.json` and per-module `/state-<id>.json`; for each entry, call `manager_->add(type, id)` and apply saved control values via `setControl()`. Seed the dirty-detection cache with the in-memory snapshot (so the first `loop10s` tick doesn't immediately re-write what was just loaded).
- `loop10s()` — snapshot the current module list + per-module state, diff against last snapshot, write changed entries to disk. Removed-module state files stay on disk (PalFs `fs_unlink` is deferred — see [backlog](../../development/backlog.md)).
