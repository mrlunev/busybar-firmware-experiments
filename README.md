# BUSY Bar Firmware Experiments

> [!WARNING]
> This is an independent experimental fork of the
> [official BUSY Bar firmware](https://github.com/busy-app/busybar-firmware).
> It is not an official firmware release and is not supported by the BUSY Bar
> team. Flash it at your own risk. A failed or incompatible update may require
> hardware recovery.

## What is different

This fork adds a JavaScript application platform while keeping the upstream
firmware and FBT build flow:

- JerryScript runtime and a `js_runner` application;
- JavaScript APIs for display, input, timers, storage, network requests,
  configuration, audio and FM radio streaming;
- dynamic discovery of applications installed under `/ext/apps`;
- seven bundled applications: Weather, FM Radio, Snake, Tetris, Doom,
  World Clock and Audio Demo;
- an Apps section in the web interface for application settings and audio
  uploads;
- native FBT packaging of `js_apps/` into the device resources.

Notion and its WebSocket/OAuth infrastructure are intentionally not included.
The fork/upstream boundary and the complete vanilla touchpoint inventory are
maintained in [FORK_MAINTENANCE.md](FORK_MAINTENANCE.md).

## Clone

The repository uses submodules, so clone it recursively:

```shell
git clone --recursive https://github.com/mrlunev/busybar-firmware-experiments.git
cd busybar-firmware-experiments
```

If the repository was cloned without submodules, initialise them with:

```shell
git submodule update --init --recursive
```

## Build

The tested hardware combination is U5 target `f22` and Si917 target `f65`.
Build and publish all artifacts with:

```shell
./fbt U5_TARGET_HW=22 SIL_TARGET_HW=65 dist
```

Published files are placed in `dist/f22-D/` for the default debug build:

- `busybar-f22-firmware-local.elf` — symbols and executable for debugging;
- `busybar-f22-firmware-local.bin` — raw U5 firmware;
- `busybar-f22-firmware-local.dfu` — U5 DFU image;
- `busybar-f22-update-local.tgz` — full update bundle;
- Si917 `.elf` and `.rps` artifacts.

Intermediate U5 artifacts are available under
`fbt_layers/fbtng/build/f22-firmware-D/fw_artifacts/`.

The output directory suffix changes with build options. For example, release
or compact builds may not use the `-D` directory name.

## Flash over USB

### Production devices

Do **not** upload `busybar-f22-update-local.tgz` from `dist/` to a production
device. The full `dist` bundle contains unsigned Si917 components.

Connect the BUSY Bar over USB and verify that its virtual Ethernet interface
is available at `http://10.0.4.20/`. Read the currently installed Si917
intercom version:

```shell
curl http://10.0.4.20/api/status/firmware
```

Use the returned `intercom_version` value in the following command:

```shell
./fbt U5_TARGET_HW=22 SIL_TARGET_HW=65 \
  INTERCOM_FORCE_VERSION=<installed-intercom-version> flash_usb
```

`flash_usb` builds and uploads a U5-only update containing the firmware and
resources. It does not replace the Si917 firmware. The generated upload bundle
is placed at:

```text
fbt_layers/fbtng/build/f22-firmware-D/flash_usb_f22.tgz
```

Normally there is no need to upload that file manually: FBT sends it to the
device and starts the update.

Do not use `flash_usb_full*` or unsigned presets on a secured production
device unless you have the required Si917 signing setup and understand the
recovery process.

### SWD

To flash only the U5 through an ST-Link or CMSIS-DAP probe:

```shell
./fbt TARGET_HW=22 flash
```

Resources are still required. Upload them separately after an SWD flash:

```shell
./fbt TARGET_HW=22 resources_upload
```

## JavaScript applications

### Runtime and native modules

`js_runner` creates a separate JerryScript context for the selected
`main.js`. Native modules are implemented in C under
`applications/main/js_runner/modules/`: each module factory builds a
JavaScript object whose properties call the corresponding firmware APIs. The
factories are registered by name in `js_modules.c` and exposed through
`require()`:

```javascript
let display = require("display");
let timer = require("timer");
```

The available native modules are `audio`, `config`, `display`, `fetch`,
`fs_extra`, `input`, `json`, `radio`, `settings`, `status`, `storage`,
`system`, `time`, `timer` and `wifi`. They are firmware bindings, not Node.js
modules; there is no npm or Node.js standard library.

All volume APIs use a normalized `0..1` value. Input and audio registrations
return an ID that can be released with `off(id)`:

```javascript
let input = require("input");
let audio = require("audio");

let inputId = input.on("ok", "short", function() {});
input.off(inputId);

let audioId = audio.on("end", function() {});
audio.off(audioId);
```

`fetch.get(url, callback)` remains available for existing apps. New code should
use the structured request API. Only one request may be active at a time; the
response body is limited to 64 KiB and response headers to 16 KiB. HTTPS uses
the firmware CA bundle and TLS stack, so server compatibility can vary:

```javascript
let fetch = require("fetch");

let requestId = fetch.request({
  url: "https://example.com/data.json",
  method: "GET",
  headers: { Accept: "application/json" },
  onProgress: function(progress) {
    print(progress.receivedBytes, progress.totalBytes);
  },
}, function(response, error) {
  if (error) {
    print(error);
    return;
  }
  print(response.status, response.ok, response.body);
});

// fetch.cancel(requestId);
```

The `time` binding exposes the configured timezone and DST-aware conversion.
Timezone arguments use the public names returned by `/api/time/tzlist` (for
example `London`, not `Europe/London`); `wifi.status()` is read-only:

```javascript
let time = require("time");
let wifi = require("wifi");

let london = time.inTimezone(time.now(), "London");
print(time.timezone(), london.hour, wifi.status().state);
```

`radio.status()` reports the native stream state, PCM rebuffering, the age of
the latest network/audio data, underrun count and current MP3/PCM buffer
occupancy. The bundled FM Radio app uses it to detect a genuinely stalled
stream without restarting on a short recoverable network gap.

### Runtime limits and failure policy

- JerryScript heap: 256 KiB per app.
- Host timers: 8.
- Active input callbacks: 16; queue capacity: 32 events.
- Built-in module cache: 16; local module cache: 16.
- Pending immediate-display primitives: 256.
- Fetch: one in-flight request and a 64 KiB buffered response.

There is no separately enforced JavaScript VM stack limit. The `js_runner`
native thread has a 24 KiB stack, but that is not a recursion guarantee for
application code. An uncaught exception from the main script or any timer,
input, fetch or audio callback is fatal to that application run: the runner
logs the exception and stack and stops its event loop.

`require("system").runtimeStats()` reports current heap use, timer/input usage,
dropped input events, callback failures and the enforced fetch limits. Input
producers are never blocked; queue overflow is counted and logged at a
rate-limited cadence.

Local JavaScript files can be loaded relative to the current file:

```javascript
let helpers = require("./helpers");
```

The `.js` suffix is optional. A local module returns the value of its final
expression, for example:

```javascript
function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

({ clamp: clamp });
```

Built-in and local modules are cached for the lifetime of the app. Globals
provided by the runner include `console`, `print`, `str`, `delay`, `require`,
`__filename`, `__dirname` and `__runLoop`. An app that uses timers, input,
fetch or another asynchronous API must call `__runLoop()` after its initial
setup.

### Create an application

1. Create a directory whose name will be the application ID:

   ```shell
   mkdir -p js_apps/hello_world
   ```

2. Add `js_apps/hello_world/app.json`. `main.js` is required for discovery;
   the manifest supplies the title shown in Apps Menu:

   ```json
   {
     "name": "Hello World"
   }
   ```

3. Add `js_apps/hello_world/main.js`:

   ```javascript
   let display = require("display");
   let timer = require("timer");

   let counter = 0;

   function render() {
     display.clear();
     display.text("Hello " + str(counter), {
       x: 2,
       y: 6,
       font: "medium",
       color: "#ffffff",
     });
     display.show();
     counter = counter + 1;
   }

   render();
   timer.every(1, render);
   __runLoop();
   ```

4. Put optional images, animations and sounds in the same directory and refer
   to them by relative filename. Apps Menu also recognises optional
   `icon_front.image` and `icon_back.image` files.

5. To expose settings in the web interface, add `config.schema.json`. Defaults
   from the schema are merged with the app's `config.json` and are available
   through `require("config").load()`:

   ```json
   {
     "title": "Hello World",
     "description": "Example application",
     "fields": [
       {
         "key": "message",
         "type": "text",
         "label": "Message",
         "default": "Hello"
       }
     ]
   }
   ```

   ```javascript
   let config = require("config").load();
   let message = config.message || "Hello";
   ```

6. Build the U5 resources:

   ```shell
   ./fbt TARGET_HW=22 resources
   ```

   FBT copies every regular file from `js_apps/` except paths beginning with
   `.` or `_`. JavaScript is not compiled into the native firmware binary: the
   app directory is preserved in the resource bundle and installed as
   `/ext/apps/hello_world`.

7. For a quick development cycle on a device that already has this fork
   installed, upload only the resources:

   ```shell
   ./fbt TARGET_HW=22 resources_upload
   ```

   For a complete U5 firmware and resources update, use the `flash_usb`
   command from the previous section. After installation, the app is
   discovered automatically in Apps Menu. It can also be launched from the
   device CLI:

   ```text
   loader open js_runner /ext/apps/hello_world/main.js
   ```

The existing applications in `js_apps/` are the most complete API examples.
The C files under `applications/main/js_runner/modules/` are the source of
truth for each binding's methods and arguments.

## Development

Generate the VS Code workspace:

```shell
./fbt vscode_dist
```

Then open `.vscode/fbt.code-workspace`. For additional targets and build
options, see the [Build System documentation](documentation/Build%20System.dox.md).
