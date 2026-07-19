# Fork maintenance

This repository is kept as a thin application-platform layer over the vanilla
BUSY Bar firmware.

## Rules

1. Fork logic lives in new fork-owned files and directories.
2. Vanilla services are consumed through their public APIs and are not modified
   to accommodate JavaScript bindings.
3. Integration patches to vanilla files are limited to application
   registration, resource collection, routes and similarly small entry points.
4. Firmware fixes that are useful without this fork are isolated in separate,
   upstream-ready commits.
5. Generated frontend files are rebuilt after an upstream merge. Conflicts in
   generated output are never resolved by hand.
6. Every modified vanilla file is recorded below. Vanilla is fetched and merged
   regularly so conflicts remain small.

New code belongs under the existing fork-owned roots:

- `applications/main/js_runner/`
- `js_apps/`
- `lib/jerryscript/`
- `lib/minimp3/`
- `lib/toolbox/mp3_decoder/`
- `lib/toolbox/pcm_output/`
- `lib/toolbox/radio_stream/`
- `assets/frontend/components/Tab/Apps/`
- `assets/frontend/stores/appConfigStore.ts`

## Vanilla touchpoints

This inventory is relative to the vanilla base commit `1b16e1b8`.

### Minimal integration

- `applications/main/application.fam` — register `js_runner`.
- `applications/system/apps_menu/app_list.c` — include discovered JS apps.
- `applications/system/apps_menu/app_list.h` — declare dynamic app entries.
- `applications/system/apps_menu/scenes/apps_menu_scene_main.c` — read JS app
  manifests.
- `scripts/fbt_env_modules/fwenv/bsb_assets.scons` — copy `js_apps/` into
  firmware resources.
- `assets/frontend/pages/index.vue` — mount the Apps tab.
- `assets/frontend/stores/tabStore.ts` — register the Apps tab.
- `lib/register_bsb_common_libs.scons` — register fork libraries.
- `targets/f21/target.json` — include fork libraries for the target.

### Compatibility patches

- `applications/services/gui/modules/image.c`
- `applications/services/gui/modules/image.h`
- `applications/services/gui/modules/label.c`
- `lib/fetch/fetch.c` — deterministic timeout and cancellation shutdown.
- `targets/f21/lwip/arch/cc.h`
- `targets/f21/lwip/arch/sys_arch.h`
- `targets/f21/lwip/lwipopts.h`

Compatibility patches should be reviewed after every vanilla merge. If vanilla
implements the same behavior, remove the local patch. If the behavior is
generally useful, move it to an isolated upstream-ready commit.

### Generated files

All files under `assets/frontend-build/public/` are generated. Rebuild them
from `assets/frontend/` after a merge.

### Documentation

- `README.md` describes this fork and intentionally differs from vanilla.

## Upstream merge procedure

1. Fetch the official repository into a temporary reference or a local
   `upstream` remote.
2. Review vanilla commits since the last merged base, especially changes to the
   files in the touchpoint inventory.
3. Merge vanilla into a dedicated integration branch.
4. Resolve source conflicts. Do not resolve generated frontend conflicts.
5. Reinstall locked frontend dependencies and rebuild
   `assets/frontend-build/public/`.
6. Build `dist`, run the JavaScript app regression set on hardware, and update
   the base commit recorded in this document.
7. Add any new vanilla touchpoint to this inventory before merging the
   integration branch.
