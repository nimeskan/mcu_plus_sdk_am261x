# Building this SDK's prebuilt libraries for CCS

Why a CCS import/build of an example in this SDK fails at the **link** step
until you first build a handful of libraries from source with `make`, where
those libraries land, and the exact commands to reproduce it. Written so a
fresh Claude Code session (or a human) in a brand new container can redo
this later in the year without re-deriving it from scratch.

This file lives at the SDK checkout's root on purpose — it's the thing you'd
go looking for from inside `mcu_plus_sdk_am261x` itself, independent of any
other repo. A companion file,
`data_workspace_c2000/environment/ENVIRONMENT_AM26X.md`, covers the rest of
the headless-CCS-for-AM26x setup (installing CCS itself, the compiler-version
fix, product registration, importing/building an example) — read that first
if CCS isn't installed yet. This file goes deep on just the library-build
step it references in its §4.

## Why this step exists at all

`c2000ware-core-sdk` (a sibling SDK for C2000 devices) ships **prebuilt**
`driverlib.lib` per device — a plain git checkout already has everything a
CCS example needs to link. `mcu_plus_sdk_am261x` does **not** ship any
prebuilt `.lib` files. Check for yourself on a fresh checkout:

```bash
find source/kernel/nortos/lib source/drivers/lib source/board/lib
# (nothing — these directories don't exist yet)
```

Every example's `example.projectspec` (e.g.
`examples/hello_world/am261x-lp/r5fss0-0_nortos/ti-arm-clang/example.projectspec`)
has a `linkerBuildOptions` block that links against libraries like:

```
-lnortos.am261x.r5f.ti-arm-clang.debug.lib
-ldrivers.am261x.r5f.ti-arm-clang.nortos.debug.lib
-lboard.am261x.r5f.ti-arm-clang.nortos.debug.lib
```

If you skip straight to `projectImport` + `projectBuild` on a fresh checkout,
every one of the example's own `.c` files compiles fine, and the build only
fails at the **final link step**:

```
error #10008-D: cannot find file "nortos.am261x.r5f.ti-arm-clang.debug.lib"
error #10008-D: cannot find file "drivers.am261x.r5f.ti-arm-clang.nortos.debug.lib"
error #10008-D: cannot find file "board.am261x.r5f.ti-arm-clang.nortos.debug.lib"
 undefined first referenced
  symbol       in file
 --------- ----------------
 _vectors
error #10234-D: unresolved symbols remain
error #10010: errors encountered during linking
```

That's not a broken environment — it means the library-build step below
hasn't been run yet. `_vectors` unresolved is the tell: it lives in
`nortos.lib`.

## What builds them, and where they land

The top-level `makefile` has a `libs:` target that delegates to a
per-device makefile (`makefile.am261x` for this device), which in turn
builds a long list of "combos" — one per module × core × toolchain × OS
combination:

```makefile
# makefile (repo root)
libs:
	$(MAKE) -C . -f makefile.$(DEVICE) libs PROFILE=$(PROFILE) DEVICE_TYPE=$(DEVICE_TYPE)

# makefile.am261x
libs: $(BUILD_COMBO_ALL)
```

`BUILD_COMBO_ALL` is the concatenation of `BUILD_COMBO_<module>` for
**every** module in the SDK — kernel, drivers, board, networking (lwIP,
enet, TSN), USB (synopsys + TinyUSB), security, mbedTLS, filesystems, and
more. Running plain `make libs` builds all of it, which is far more than a
single example needs and takes a long time. The three combos `hello_world`'s
`r5fss0-0_nortos` / `ti-arm-clang` configuration actually needs are:

```makefile
BUILD_COMBO_nortos  = nortos_r5f.ti-arm-clang nortos_r5f.iar-arm
BUILD_COMBO_drivers = drivers_r5f.ti-arm-clang.nortos drivers_r5f.ti-arm-clang.freertos drivers_r5f.ti-arm-clang.freertos_mpu drivers_r5f.iar-arm.nortos drivers_r5f.iar-arm.freertos drivers_r5f.iar-arm.freertos_mpu
BUILD_COMBO_board   = board_r5f.ti-arm-clang.nortos board_r5f.ti-arm-clang.freertos board_r5f.ti-arm-clang.freertos_mpu board_r5f.iar-arm.nortos board_r5f.iar-arm.freertos board_r5f.iar-arm.freertos_mpu
```

i.e. `nortos_r5f.ti-arm-clang`, `drivers_r5f.ti-arm-clang.nortos`, and
`board_r5f.ti-arm-clang.nortos`. Each combo target `cd`s into the module's
subdirectory and compiles+archives just that library:

| Combo target | Output library | Object directory |
|---|---|---|
| `nortos_r5f.ti-arm-clang` | `source/kernel/nortos/lib/nortos.am261x.r5f.ti-arm-clang.<profile>.lib` | `source/kernel/nortos/obj/am261x/ti-arm-clang/<profile>/r5f/nortos/nortos/` |
| `drivers_r5f.ti-arm-clang.nortos` | `source/drivers/lib/drivers.am261x.r5f.ti-arm-clang.nortos.<profile>.lib` | `source/drivers/obj/am261x/ti-arm-clang/<profile>/r5f/drivers/nortos/` |
| `board_r5f.ti-arm-clang.nortos` | `source/board/lib/board.am261x.r5f.ti-arm-clang.nortos.<profile>.lib` | `source/board/obj/am261x/ti-arm-clang/<profile>/r5f/board/nortos/` |

(`<profile>` is `debug` or `release`, from `PROFILE=`.) These paths are
exactly what the example's `linkerBuildOptions` searches via `-i` (library
search path) — `${MCU_PLUS_SDK_PATH}/source/kernel/nortos/lib`,
`.../source/drivers/lib`, `.../source/board/lib` — so once the libraries
exist there, no project-side change is needed; re-running `projectBuild`
just finds them.

## Generalizing: figuring out which combo(s) a *different* example needs

The above is specific to `hello_world` on NoRTOS. For any other example:

1. Open that example's `<board>/<core>_<os>/<compiler>/example.projectspec`
   and look at the `linkerBuildOptions` inside the `<configuration>` block
   you're building (`Debug` or `Release`). The `-l<name>.lib` entries are
   exactly the libraries you need built first.
2. Strip the `am261x.r5f` device/core tokens and the `.debug`/`.release`
   suffix from each library name to get back to a module name, then grep
   `makefile.am261x` for `^BUILD_COMBO_<module>` to see the exact combo
   target strings available for that module — pick the one whose OS/toolchain
   suffix matches what you saw in the `.lib` name (`.nortos` vs `.freertos`
   vs `.freertos_mpu`; `.ti-arm-clang` vs `.iar-arm`).
3. The kernel library's combo name doesn't follow the `<module>_r5f.<toolchain>.<os>`
   pattern — it's OS-specific at the top level:
   ```makefile
   BUILD_COMBO_nortos   = nortos_r5f.ti-arm-clang nortos_r5f.iar-arm
   BUILD_COMBO_freertos = freertos_r5f.ti-arm-clang freertos_r5f-mpu.ti-arm-clang freertos_r5f.iar-arm
   ```
   i.e. a FreeRTOS example needs `freertos_r5f.ti-arm-clang` (producing
   `source/kernel/freertos/lib/freertos.am261x.r5f.ti-arm-clang.<profile>.lib`),
   not `nortos_r5f.ti-arm-clang`.
4. Examples that pull in networking/USB/security also need the matching
   `BUILD_COMBO_<that module>` combo built the same way — same recipe, just
   a longer `BUILD_COMBO_ALL` override in the command below.

## Prerequisite: CCS + the *matching* compiler version

You need a working CCS install with the AM2x/Sitara component
(`PF_SITARA_MCU`, not the C2000 `PF_C28`) before any of this — see
`ENVIRONMENT_AM26X.md` §1 for the full install command. That alone is not
quite enough, though:

**Compiler-version pitfall.** This SDK's example `.projectspec` files (and
`imports.mak`'s default `CGT_TI_ARM_CLANG_PATH`) pin
`ti-cgt-armllvm 4.0.4.LTS`. CCS 21.0.0's offline installer only ships
**5.1.1.LTS** — noticeably newer. Building `nortos.lib` with 5.1.1.LTS fails:

```
dpl/r5/HwiP_armv7r_handlers_nortos.c:65:51: error: interrupt service routine
with vfp enabled may clobber the interruptee's vfp state; consider using the
`interrupt_save_fp` attribute to prevent this behavior
[-Werror,-Warm-interrupt-vfp-clobber]
```

5.1.1.LTS added a new diagnostic that fires on this SDK's own Cortex-R5
interrupt vector handlers; combined with the SDK's `-Werror` that's a hard
build failure — 6 errors, and `nortos.lib` never gets produced. (`drivers`
and `board` don't hit this file, so they build fine even with 5.1.1.LTS —
only `nortos` needs the older compiler.) Fix: install the standalone
4.0.4.LTS compiler once, and point every `make libs` invocation at it
explicitly rather than whatever CCS bundled:

```bash
curl -sSL -o ti_cgt_armllvm_4.0.4.LTS_linux-x64_installer.bin \
  "https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-ayxs93eZNN/4.0.4.LTS/ti_cgt_armllvm_4.0.4.LTS_linux-x64_installer.bin"
chmod +x ti_cgt_armllvm_4.0.4.LTS_linux-x64_installer.bin
./ti_cgt_armllvm_4.0.4.LTS_linux-x64_installer.bin \
  --mode unattended --unattendedmodeui none \
  --prefix /root/ti/ti-cgt-armllvm_4.0.4.LTS

# the installer nests its own versioned dir one level inside --prefix; flatten it
mv /root/ti/ti-cgt-armllvm_4.0.4.LTS /root/ti/ti-cgt-armllvm_4.0.4.LTS_tmp
mv /root/ti/ti-cgt-armllvm_4.0.4.LTS_tmp/ti-cgt-armllvm_4.0.4.LTS /root/ti/ti-cgt-armllvm_4.0.4.LTS
rmdir /root/ti/ti-cgt-armllvm_4.0.4.LTS_tmp
```

(Check `https://www.ti.com/tool/download/ARM-CGT-CLANG` for the current
version/URL if `4.0.4.LTS` is no longer current when you read this — TI
ships new compiler releases regularly, and the SDK's pinned `cgtVersion`
may have moved on too. Check `cgtVersion=` in an example's
`example.projectspec`, or `CGT_TI_ARM_CLANG_PATH` in `imports.mak`, to see
what version *this* checkout expects.)

If you also want CCS itself (GUI/CLI project import, not just this `make`
step) to see the compiler as an installed option, symlink it into CCS's own
compiler directory — see `ENVIRONMENT_AM26X.md` §2 for that and why it
matters for `projectImport`. It is **not** required for the `make libs`
step in this file, which is pointed at the compiler directly via
`CGT_TI_ARM_CLANG_PATH` regardless of what CCS knows about.

## `imports.mak`'s defaults are stale here — override them

`imports.mak` (included by the top-level `makefile`, but **not** by
`makefile.am261x` when that's invoked directly) sets these Linux defaults:

```makefile
export CCS_PATH?=$(TOOLS_PATH)/ccs2050/ccs        # TOOLS_PATH defaults to $(HOME)/ti
CGT_TI_ARM_CLANG_PATH=$(CCS_PATH)/tools/compiler/ti-cgt-armllvm_4.0.4.LTS
SYSCFG_PATH ?= $(TOOLS_PATH)/sysconfig_1.27.0
```

None of these match a from-scratch CCS 21.0.0 install:

- `CCS_PATH` assumes a CCS **20.5.0** install (`ccs2050`) — the SDK docs'
  recommended version. If you installed 21.0.0 per `ENVIRONMENT_AM26X.md`,
  it's actually at `.../ccs2100`. `CCS_PATH` uses `?=`, so exporting it from
  the shell before calling `make` is enough to override it.
- `SYSCFG_PATH` assumes a standalone `sysconfig_1.27.0` install. CCS 21.0.0
  instead bundles **1.28.0** inside `$CCS_PATH/utils/sysconfig_1.28.0` (no
  separate standalone install needed — that's what CCS's own SysConfig CLI
  build step uses too). Also `?=`, same fix.
- `CGT_TI_ARM_CLANG_PATH` is a **plain `=` assignment**, not `?=` — exporting
  it from the shell does *not* override it (a non-`?=` assignment in a
  makefile always wins over an inherited environment variable of the same
  name). You must pass it as a `make` command-line variable
  (`make ... CGT_TI_ARM_CLANG_PATH=...`), which does take precedence over
  any in-file assignment.

## The command

From the repo root:

```bash
cd mcu_plus_sdk_am261x   # this SDK's checkout root

export CCS_PATH=/root/ti/ccs2100/ccs                       # wherever your CCS install actually is
export SYSCFG_PATH="$CCS_PATH/utils/sysconfig_1.28.0"      # check the real version: ls "$CCS_PATH/utils"
export PATH="/root/.local/bin:$PATH"                        # only if you used the udev/service shim to install CCS

make libs PROFILE=debug DEVICE_TYPE=GP \
  CGT_TI_ARM_CLANG_PATH=/root/ti/ti-cgt-armllvm_4.0.4.LTS \
  BUILD_COMBO_ALL="nortos_r5f.ti-arm-clang drivers_r5f.ti-arm-clang.nortos board_r5f.ti-arm-clang.nortos" \
  -j4
```

Notes on the flags:

- **`PROFILE=debug`** builds the `*.debug.lib` variants that a `Debug`
  build configuration links against; use `PROFILE=release` for `Release`.
  Build both if you want both configurations to work.
- **`DEVICE_TYPE=GP`** (general-purpose) is the default and matches the
  `am261x-lp`/`am261x-som` eval boards used by the examples in this repo;
  leave it unless a specific example's docs say otherwise (HS/security
  variants use `DEVICE_TYPE=HS`).
- **`BUILD_COMBO_ALL=...` overridden on the command line** is what narrows
  the huge default list down to just the combos you need. This works
  because GNU Make automatically forwards command-line variable overrides
  to `$(MAKE)` sub-invocations (the top-level `libs:` recipe's own
  `$(MAKE) -C . -f makefile.$(DEVICE) libs ...` call) — so the override you
  set at the top level reaches into `makefile.am261x`'s
  `libs: $(BUILD_COMBO_ALL)` rule and replaces its file-level definition,
  same precedence reasoning as the `CGT_TI_ARM_CLANG_PATH` note above.
- **`-j4`** is safe — each combo is an independent subtree with its own
  `obj/`/`lib/` output, no cross-combo file contention.

Expect on the order of a minute or two for these three combos (versus much
longer for the full unrestricted `make libs`, which also builds every
networking stack, USB stack, and security module in the SDK).

## Cleaning up afterward

This repo has **no top-level `.gitignore`** for build output (unlike
`data_workspace_c2000/workspaces/.gitignore`, which does exclude CCS build
directories). That means `obj/` and `lib/` under each module show up as
untracked files in `git status` after a build — don't commit them into this
vendor SDK checkout. Either:

```bash
# targeted: only what the command above created
rm -rf source/kernel/nortos/obj source/kernel/nortos/lib \
       source/drivers/obj/am261x/ti-arm-clang/*/r5f/drivers/nortos \
       source/board/obj/am261x/ti-arm-clang/*/r5f/board/nortos
# (drivers/board obj dirs also hold other combos' output if you built more than the three above —
#  delete the whole source/{drivers,board}/{obj,lib} tree if you're not sure what else is in there)
```

or use the SDK's own cleanup targets, restricted the same way `libs` was:

```bash
make libs-clean PROFILE=debug \
  BUILD_COMBO_CLEAN_ALL="<matching *_CLEAN combo names — check makefile.am261x>"
# or, for a full wipe of everything ever built here:
make libs-scrub
```

`git status` (clean = nothing untracked under `source/`) is the check that
you got it all before pushing anything else in this repo.

## End-to-end summary (fresh container, nothing installed yet)

1. Install CCS 21.0.0 with `--enable-components PF_SITARA_MCU` — see
   `ENVIRONMENT_AM26X.md` §1 (needs the udev/`service` shims from
   `ENVIRONMENT.md` §3 first, in a container without udev).
2. Install the standalone `ti-cgt-armllvm 4.0.4.LTS` compiler — §2 above
   and `ENVIRONMENT_AM26X.md` §2 (also symlink it into CCS's
   `tools/compiler/` if you'll use CCS's `projectImport`/GUI, not just this
   `make` step).
3. Register this SDK checkout with CCS's product discovery —
   `ENVIRONMENT_AM26X.md` §3 (already ships
   `.metadata/.tirex/package.tirex.json`, nothing to recreate).
4. **Build the libraries this file documents** — the command above.
5. Import and build an example — `ENVIRONMENT_AM26X.md` §5, or generalize
   per the "figuring out which combo(s)" section above for a different
   example/board/OS.
