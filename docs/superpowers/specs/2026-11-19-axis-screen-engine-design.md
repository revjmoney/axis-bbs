# AXIS Project 2: the screen and menu engine, the in-BBS sysop panel, and the art packs

Status: design, for Rev's approval. Written 2026-11-19. Branch `pc/screens` (worktree F:\axis-bbs-screens), cut from
`pc/app` 8aa6350 (project 1a complete). Parent design: `docs/superpowers/specs/2026-11-19-axis-pc-design.md` (its
project 2).

## Why

AXIS should feel like one coherent system: every section (login, messages, files, doors, chat, user settings, the
sysop panel) built from the same parts, so a theme restyles all of it and a sysop can drop in menu sets and art packs
they already own. Today it is partly there (ViSiON/3-style `.MNU` files over theme `.ANS` art, see docs/MENU-ART.md),
but a short code set, several screens still drawn in C++, and only one menu format read leave seams. This project
closes them, in the shared core so the boards benefit where they fit.

## Goals

1. **Complete theme coverage.** Every caller-facing screen comes from theme art plus a menu or list definition, not
   from code: convert the remaining hard-coded screens (the file and message browsers, the user list, the sysop
   shell, the user-settings screens, the stats/About/who screens) to the themeable list-and-menu system.
2. **A wider code set.** The screen renderer understands the pipe/MCI/@ codes the target families use, so a sysop's
   existing art renders correctly: ViSiON/3 and Renegade pipe codes first, then Mystic MCI, Synchronet Ctrl-A and
   `@` codes, then WWIV codes. (From the art survey, `F:\axis-bbs\local\menupacks\REPORT.md`.)
3. **Menu import.** Read other families' menu definitions into AXIS menus: ViSiON/3 JSON, Oblivion2-XRM YAML, WWIV
   JSON, Mystic text; a converter for binary Renegade/Telegard `.MNU` when a sysop has real ones. Import, not a live
   dependency: the imported menu becomes a native AXIS `.MNU`.
4. **The in-BBS sysop panel.** Grow the existing sysop menu (`src/bbs/sysopshell`) into the full "normal sysop stuff"
   panel, themed and lightbar-driven, reachable from the WFC, telnet and the window: users (edit, validate, set
   access, delete, twit), message and file areas (add, edit, move and remove posts and files, validate uploads),
   system config (name, ports, new-user defaults, theme), nodes and callers (who's on, chat, kick, give time), logs
   and stats, and the door and menu config. It is the same screen engine, so it shares the look.
5. **The doors menu as a lightbar**, like every other list.
6. **Host the art packs.** An Art Packs file area and the ANSI pack viewer; the repo-safe packs (ViSiON/3 MIT, Mystic
   P1XArion CC0, Synchronet GPL v2+, Oblivion2-XRM zlib, WWIV Apache-2.0, each with the survey's listed exclusions)
   ship in the repo; the 16colo scene packs stay local unless their artists give permission (Rev's call per pack).

## Non-goals

- A rewrite of the menu system: AXIS already has ViSiON/3-style menus; this extends them. No new menu language.
- Running other BBS packages' code: their art and menus are read/imported, not executed; no Go/JS/Pascal is linked.
- A live sync to another BBS's config: a menu is imported once into a native `.MNU`.
- A separate sysop app: the panel is in the BBS (Rev chose this), not a desktop or web app.
- Pixel-perfect emulation of every pack: the goal is "renders correctly and legibly", not byte-identical to the
  original BBS.

## Anti-goals (failure even if it works)

- A screen that still only exists in C++ after this project, so a theme cannot touch it.
- A sysop's real art pack that renders as garbage (unknown codes printed, wrong colours, scrolled off screen).
- The board build growing past its flash line (2,200,000 B) from the wider code set or the panel.
- The panel exposing something over telnet that should be console-only, or showing private details (the privacy
  rules from the stats work still hold).
- Importing a menu that silently drops commands AXIS cannot run (an import must report what it could not map).

## Approaches considered (the renderer)

| | Approach | Trade-off | Verdict |
|---|---|---|---|
| A | One renderer in the core that understands every family's codes via a dialect switch per screen | One code path, the boards get it too; the dialect is detected or set per theme | **Chosen** |
| B | A preprocessor that converts foreign art to AXIS's own codes at import time | Keeps the runtime renderer small; but loses round-trip fidelity and needs a converter per format | Folded into A for menus (import), not art (render live) |
| C | A separate PC-only renderer | Splits the boards from the PC, against the whole project | Dropped |

Chosen: **A for live art rendering** (the renderer reads the dialect's codes directly, so a dropped-in `.ANS` with
Mystic or Synchronet codes just works), **B for menus** (menu definitions are imported/converted once into native
`.MNU`, since menu logic must map to AXIS commands anyway).

## How it is built (what Rev decided, and the plan's shape)

- **Shared core**, extending `src/bbs`: the screen renderer (today's pipe-code expander in node.cpp / pipecodes.cpp),
  the menu system (menufile/menurunner), the list/lightbar widget (lightbar.cpp, ansiviewer's ScrollList), and the
  theme system (theme.cpp). The boards get what fits within flash; PC-only extras go behind the feature switches
  from project 1a.
- **The code set** grows as a dialect table: each code family (ViSiON pipe, Renegade `%`, Mystic MCI and `|[X`,
  Synchronet Ctrl-A and `@`, WWIV Ctrl-C and `|#`) maps to the same internal attributes and value tokens. A theme
  (or a screen's SAUCE/first-line hint) picks the dialect; ANSI/CP437/iCE colour, cursor save/restore and absolute
  positioning are shared (the survey's baseline).
- **Complete coverage** converts each remaining hard-coded screen into a themed screen plus a list or menu
  definition, one screen per task, keeping the exact behaviour and the privacy rules.
- **The in-BBS panel** is the sysop menu (`sysopshell`) rebuilt on the list/menu engine: one themed, lightbar panel
  with the sections in Goal 4, the actions wired to the portable sysop commands from project 1a (`runPortableCommand`)
  plus the board-only ones where run locally.
- **Menu import** is a tool/command that reads a foreign menu set and writes native `.MNU`/`.ANS`, reporting unmapped
  commands.
- **Art packs**: the repo-safe packs go in a new `art/packs/` with their licences and the survey's exclusions; the
  BBS seeds them into an Art Packs file area and lists them in the ANSI pack viewer.

## Roadmap within project 2 (each its own plan task, order)

1. The dialect-based code set (ViSiON/Renegade first), with the shared ANSI/colour/positioning baseline and tests
   against real pack screens.
2. Complete coverage: convert the file browser, message browser, user list, user settings and the who/stats/About
   screens to themed list-and-menu definitions (several tasks, one screen group each).
3. The in-BBS sysop panel on the engine (the sections in Goal 4).
4. The doors menu as a lightbar.
5. Menu import (ViSiON/3 JSON, XRM YAML, WWIV JSON, Mystic text; the Renegade binary converter).
6. The remaining code families (Mystic, Synchronet, WWIV) as the packs that need them are hosted.
7. Art Packs area and the repo-safe packs, with the ANSI pack viewer.
8. Testing at the end (Rev's rule): unit tests for the dialect table and each converted screen, a flashed-board
   walk, and the PC walk; screenshots of each pack rendering.

Order reasoning: the code set (1) and coverage (2) are the engine everything else draws on; the panel (3) and doors
(4) use it; import (5) and the extra families (6) widen reach; the packs (7) are the payoff; testing last.

## Calls made in this design (Claude's, for Rev to check)

- Branch `pc/screens` off `pc/app` (project 1a), so the PC foundation comes along; nothing pushed.
- Live rendering for art (dialect switch), import/convert for menus; not a live foreign-config dependency.
- The in-BBS panel reuses the sysop menu and the project 1a portable commands, not a new command set.
- Repo-safe art packs ship with their licences; scene packs stay local pending per-pack permission (Rev decides).
- ViSiON/Renegade codes and the four target families' screens first; others as their packs are hosted.
- The boards get the engine where it fits under the flash line; anything that does not fit is PC-only behind a switch.

## Resolved from the builder check (Claude's calls, 2026-11-19)

- **"Convert to theme art plus a list" means the body too, via a list template.** A themed header over a C++ body
  is not enough: complete coverage means a theme can restyle the columns, the per-row layout and the status line.
  So the engine gains a lightweight list-template format (a header line, a repeated row template with value tokens,
  a footer/status line, the `.TOP/.MID/.BOT` style the art survey names). This is NOT a new menu language (menus
  stay `.MNU`, command logic unchanged); it is a layout template for list rows. A screen with no template falls back
  to today's C++ layout, so conversion is incremental and nothing breaks mid-project.
- **Board-versus-PC split is decided per task against the flash line, not guessed.** The board gets the shared
  baseline (ANSI, CP437, iCE colour, cursor save/restore, positioning), the ViSiON/Renegade dialect, and the
  converted screens and list templates that fit under 2,200,000 B. The heavier dialects (Mystic, Synchronet, WWIV)
  and the import tooling are PC-only behind the project-1a feature switches. Every task measures the cyd image and
  moves anything that would cross the line to PC-only.
- **The anti-goal is the acceptance bar, done incrementally.** No caller-facing screen stays C++-only by the end.
  Goal 1's named screens are the first coverage wave; the rest (Your Stats, Last Callers, the rumor line, the other
  Oblivion/Iniquity extras) follow in later coverage tasks.
- **The board's 40-column touch console stays on today's renderer.** It is board hardware UI (tapRow touch, 7 items
  a page). The engine-based panel serves the 80-column telnet, window and WFC views; the board keeps its touch panel.
- **The panel's nodes-and-callers actions already exist** (`@KICK`, `@ADDTIME`, `@NOTE`, SysOp Chat), so the panel
  reuses the project-1a portable commands with no new command set, as promised.

## Imported packs are runnable looks (Rev, 2026-11-19)

An imported menu set from any target family (ViSiON, Mystic, Oblivion, WWIV, and Renegade via the binary converter)
becomes a **runnable look**: a sysop picks "run my board as <pack>" and the whole board runs in that pack's menus and
layout. Consequences for the design:

- **The menu window is no longer fixed.** Today AXIS uses one window (rows 9-21, cols 4-44, docs/MENU-ART.md). Each
  look declares its own geometry: a menu's `.MNU` already carries each bar's `x,y`, so a pack's own bar positions are
  honored directly, and a look declares its window/prompt placement in its theme (a small per-look geometry block,
  not a new language). AXIS's current window becomes the default when a look declares none, so existing looks are
  unchanged.
- **Every section's menu comes from the active look's menu set**, so switching look restyles the whole board, screen
  layouts included, not just the art. The engine resolves each screen as: the active look's own `.MNU`/`.ANS`/list
  template, falling back to AXIS's default set.
- **Import maps commands and preserves layout.** The importer writes a native `.MNU` per screen with the pack's bar
  positions kept and each foreign command mapped to an AXIS command (reporting any it cannot map, per the anti-goal),
  plus the pack's `.ANS` art and a list template where the screen is a list.
- **A look that a pack does not cover falls back** to AXIS's default screen for that section, so a partial pack still
  runs (it does not leave a caller at a blank screen).
- **Scope note:** this makes the theme/look system the backbone of the project. The roadmap's order holds, but the
  list-template work (task 2) and the menu loader (tasks 3-5) now also carry the per-look geometry; the plan sizes
  each task against the flash line as above, and per-look geometry on the board is PC-first if it does not fit.

## Left to each task's plan

The exact dialect-table shape, how a theme declares its dialect, the list widget's API for each converted screen, the
panel's section layout and ACS gating, the import tool's CLI and its unmapped-command report format, and the art area
seeding.
