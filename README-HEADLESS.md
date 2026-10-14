# kicad-headless

**Headless agent support for KiCad** — expose KiCad's core capabilities (schematic capture, placement, routing) as CLI commands and APIs, so autonomous agents can drive the full PCB design flow without a GUI.

> This project runs as a fork of [KiCad](https://www.kicad.org) (`pns-headless` branch) and may upstream pieces back over time.

## Why

KiCad's official CLI (`kicad-cli`) covers DRC, export and plotting — but the *design* capabilities (routing, placement, schematic capture) are GUI-only. Agents that can plan a design have no way to execute it. This project closes that gap, one capability at a time, using KiCad's own engines (PNS push-and-shove for routing, the connectivity system for validation) so results are indistinguishable from interactive work.

## Capabilities

| Module | Status | Entry point |
|---|---|---|
| **Routing** (PNS push-and-shove, headless) | 🟡 prototyping | `kicad-cli pcb route` |
| **Placement** | ⚪ planned | `kicad-cli pcb place` |
| **Schematic capture** | ⚪ planned | `kicad-cli sch ...` |

## Routing (current prototype)

```
kicad-cli pcb route \
    --net P1_HS_DP \
    --from-x 36.1 --from-y 8.8 \
    --to-x 34.9 --to-y 9.3 \
    --layer F.Cu \
    board.kicad_pcb
```

- Uses the same [PNS](https://docs.kicad.org) push-and-shove engine as the interactive router — results are board-rule-correct, obstacles are shoved aside rather than ignored.
- `PNS_BRIDGE` (pcbnew/router/pns_headless_api.{h,cpp}) wraps the PNS router for headless (no VIEW, no TOOL_MANAGER) operation; `kicad-cli pcb route` drives it through the standard JOB pipeline.

## Build

Same dependencies as upstream KiCad. Two targets matter here:

```sh
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DKICAD_BUILD_PNS_DEBUG_TOOL=ON ../../
ninja pcbnew_kiface kicad-cli
```

CI (`.github/workflows/build-pns.yml`): Linux on the official KiCad Fedora container; Windows via vcpkg manifest.

## Repository layout

- `pcbnew/router/pns_headless_api.{h,cpp}` — headless PNS bridge (the core)
- `kicad/cli/pcb_route_command.{h,cpp}` — CLI surface
- `pcbnew/job_pcb_route.cpp` — job handler (KiCad JOB pipeline)
- `_pns_headless_patch/` — original design notes (kept for history; installed code lives in-tree)

## License

GPLv3+ — matching upstream KiCad.
