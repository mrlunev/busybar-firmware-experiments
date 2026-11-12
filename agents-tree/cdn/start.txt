# BUSY Bar: start here

Instructions for AI coding agents helping a person make something for BUSY Bar. This page only picks the
instructions that fit. Pick one path below, open its document and follow it. Do not mix paths.

## 1. Check first

These checks only read; run them yourself before asking anything.

- **Already in a BUSY Bar app project?** If the working folder has `src/appmeta/manifest.json`, read the project's
  own `AGENTS.md` and follow it. If the person has no BUSY Bar and that file does not mention BUSY Bar Emulator,
  also use the emulator's path from the table below, without its `create` step. Stop here.
- **BUSY Bar on USB?** `curl -s -m 3 http://10.0.4.20/api/status/firmware` (in PowerShell: `curl.exe`). JSON with
  `version` means a BUSY Bar is connected, and `version` is its firmware. No answer means none on USB; the person
  may still have one on Wi-Fi.
- **BUSY Bar Emulator installed?** Its pointer file exists:
  `~/Library/Application Support/BUSY Bar Emulator/install.json` on macOS,
  `%LOCALAPPDATA%\BUSY Bar Emulator\install.json` on Windows.

## 2. Ask only what the checks cannot tell

- **What to make**, if the person did not say: an app that runs on BUSY Bar, a widget that runs on their computer,
  or a change to the firmware. When unsure, it is an app.
- **Whether they have a BUSY Bar**, if none answered on USB and they did not say: on Wi-Fi (ask its address), or
  none.

If there is nobody to ask, make an app: on BUSY Bar if one answered, otherwise on BUSY Bar Emulator.

## 3. Paths

| The person wants | BUSY Bar | Open and follow |
|---|---|---|
| An app that runs on BUSY Bar | on USB or Wi-Fi | [JS apps on BUSY Bar](https://raw.githubusercontent.com/busy-app/busybar-firmware/dev/applications_js/AGENTS.md) |
| An app that runs on BUSY Bar | none | [JS apps on BUSY Bar Emulator](https://raw.githubusercontent.com/busy-app/busybar-emulator/main/public-docs/agents.md) |
| To send a finished app to the app catalog | any | [Catalog: assisting an application author](https://raw.githubusercontent.com/busy-app/busy-apps-catalog/main/AGENTS.md), then its [CONTRIBUTING](https://raw.githubusercontent.com/busy-app/busy-apps-catalog/main/CONTRIBUTING.md) |
| A widget: a program on their computer that shows things on BUSY Bar through its HTTP API | on USB or Wi-Fi | [Python library guide](https://raw.githubusercontent.com/busy-app/busylib-py/main/AGENTS.md); walkthrough: [Widget with AI](https://docs.busy.app/bar/dev/widget-with-ai.md) and [HTTP API](https://docs.busy.app/bar/dev/http-api.md) |
| A change to the firmware itself | any | [Firmware](https://raw.githubusercontent.com/busy-app/busybar-firmware/dev/AGENTS.md): clone the repository and work in it |

An app is a JavaScript app that runs on BUSY Bar itself and goes to the app catalog. A widget runs elsewhere and
needs that computer to stay on. Native firmware apps break official updates; build one only when asked to.

## 4. When sources disagree

- **The HTTP API is what the BUSY Bar's firmware says it is.** Its OpenAPI spec for a given firmware version:
  `https://api.busy.app/busybar/openapi.yaml?Name=<version>`, for example `Name=1.3.1`. Paths there start with
  `/busybar/`; on the BUSY Bar itself they start with `/api/`. When a guide shows a field or value the spec does
  not have, the spec is right.
- **Endpoints differ between firmware versions.** Check the version first, as in step 1, and use only what that
  version has.
- The tools are the npm packages `@busy-app/create-app`, `@busy-app/cli` and `@busy-app/busy-lib`. The npm
  package named plain `busy-cli` is not ours: never install it.

## Known pitfalls

Some documents are behind the tools. Until they are fixed:

- **Catalog builds need npm or a pinned pnpm.** The catalog accepts `package-lock.json`, or `pnpm-lock.yaml` with
  `"packageManager": "pnpm@<version>"` in `package.json`. `busy-cli create` picks pnpm and does not add that field:
  choose npm, or add the field yourself. Yarn and Bun projects are refused.
- **Settings:** `loadValues()` from the template's `@shared/settings` always returns `null`. Read
  settings with `device.AppsSettingsGet({ app_id })` from `@busy-app/busy-lib`. Apps can read settings, not write
  them, and read them only at start.
- **busy-lib:** the installed version is 0.21.1 (`device.DisplayDraw`). The README on GitHub describes 1.0.0-rc.1
  (`displayDraw`, `BusyBarAppClient`), which a new project does not have.
- **busy-tools has no `AGENTS.md`.** Where a document says to read it, read the README and the new project's
  `AGENTS.md` instead.
- **docs.busy.app pages that say there is no app catalog yet are out of date.**

## More

- Docs index for agents: https://docs.busy.app/llms.txt ; any docs.busy.app page also reads as Markdown with `.md`
  added to its address.
- Docs for people: [JS Apps Docs](https://go.busy.app/js-apps), [Emulator Docs](https://go.busy.app/busy-emulator-docs).
