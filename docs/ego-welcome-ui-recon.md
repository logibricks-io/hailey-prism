# ego lite UI recon — welcome page, onboarding, spaces overview

Recon performed 2026-08-28 against `/Applications/ego lite.app` (v0.4.7.3, Chromium-based).
Goal: close the visual/interaction parity gaps flagged by the user (welcome page,
tab/omnibox brand chip, top-right agent badge, spaces transition).

## 1. Resource extraction (how the assets were obtained)

ego ships its WebUI app (welcome, onboarding, migration, whats-new) as a Vite/React
bundle packed into `ego Framework.framework/Resources/resources.pak`:

- PAK v5, but with a nonstandard header: `u32 version=5, u32 encoding, u16 num_aliases, u16 num_resources` (12 bytes total), followed by `num_resources` entries of `(u16 id, u32 offset)` (6 bytes each); entry length = next offset - offset.
- Most entries above ~2 KB are stored **gzip-compressed** (magic `1f 8b`) with no FNAME. Decompress each entry, then identify by content.
- Extraction tooling used: `/tmp/ego-pak/unpak.py` + `inflate.py` (ephemeral, not in repo).

The extracted reference set lives in `chromium/reference/ego-welcome/`:

- `index.html` — shell page served at `ego://welcome/` (title "Welcome to ego (𝘭𝘪𝘵𝘦)", loads `../js/welcome-aboard-*.js`, `../styles/index.css`, `../styles/vendor-misc.css`, svg favicon).
- `js/welcome-aboard-entry.js` — the page entry chunk (31.5 KB minified, fully readable; contains ALL copy, DOM structure, and interaction logic). `welcome-aboard-pretty.js` is a lightly reformatted copy.
- `css/index.css` (187 KB) — Tailwind v4 compiled output + design tokens (`--bg-1`, `--text-1..4`, `--fg-4`, `--bg-alpha-4`, …) + `@font-face` Kulim Park + all utility classes used by the page.
- `css/welcome-embedded.css` — 1078-char runtime-injected CSS from the entry chunk (cards container queries: `.welcome-aboard-cards-viewport/row/stage`, scale `min(1, calc(100cqw / 1440px))`).
- `css/*.css` — other cluster stylesheets (start-motion = onboarding transitions, whats-new, skeletons, KaTeX).
- `assets/` — all raster/vector art (see inventory below).
- `fonts/` — Kulim Park ExtraLight / ExtraLight Italic / SemiBold (display font used by onboarding headings; welcome page itself uses system fonts).

## 2. Welcome page (`ego://welcome`) — structure and behavior

Dark theme (`body { background: var(--bg-1) }`). Layout: centered column,
`flex flex-col items-center gap-12 pt-12`.

Header (`Re` component in entry JS):
- Glyph logo: inline component `te`, rendered 78×48, colored `text-(--fg-4)` (muted). Art = three overlapping ellipses (`assets/ego-glyph-*.png`, `ego-mark-*.svg`).
- `h1`: "Welcome<br>aboard!" — `font-black text-(--text-2) text-[64px] leading-[0.85]`.
- Subtitle: "Follow the guides below to get started" — `max-w-90 text-(--text-3) text-base`.

Cards row: three cards, flex wrap, gap 12px; viewport uses container queries so the
whole row scales down proportionally on narrow windows (observed: at ~800 px width
cards wrap 2+1).

Card A — "ego (lite) is ready to work with your agents"
- Copy: "Your agent now runs browser tasks faster, reuses past successes, and uses fewer tokens."
- Decorative animated arcs (green/yellow/pink bezier strokes, CSS/SVG animation).
- Frosted tip card (backdrop blur) with two bullets:
  - "To start using ego, **restart your agent.**"
  - "If ego (lite) is unavailable or the skill is missing, manually install it by running `npx skills add citrolabs/ego-lite` in your agent."

Card B — "Try ego (lite) with your ⌗ agent"
- Sub: "Type /ego-browser followed by your task in the agent, or specify using ego browser in your prompt."
- "Open in" row: agent selector dropdown (default **Codex**) + arrow button (opens deep link).
  - Dropdown options with icons: Claude Code, Codex, OpenCode, Cursor, Hermes (Kiro and OpenClaw also present in the list data). Icons: Claude Code / Codex / Kiro / OpenClaw / OpenCode are inline `data:image/svg+xml` in the entry JS; Cursor = `assets/cursor-*.svg` (not yet located in pak), Hermes = `assets/hermes.svg` (NousResearch mark, extracted).
  - Deep-link map (from entry JS): Codex `codex://new?prompt=`, Claude Code `claude://code/new?q=`, Cursor `cursor://anysphere.cursor-deeplink/prompt?text=`, OpenCode `opencode://open` (app url), Hermes `hermes://open` (app url).
- Divider: "or copy and paste in your terminal" (dashed lines both sides).
- Prompt line: `/ego-browser OpenAI & Anthropic blogs, summarize latest noteworthy updates` + copy button (turns into "Copied" check for 1.5 s).
- Terminal mock: mini fake session — "Claude Code v2.1.159 / Opus 4.8", `❯ /ego-browser summarize OpenAI & Anthropic blogs`, streamed reply text; rendered by an inner component scaled `0.72`, clipped in a rounded card (`h-57 w-104.5`, `origin-top-left`).

Card C — "Discover how ego (lite) can enhance your life"
- Tutorial link card: beach thumbnail (`assets/tutorial.png`), "Tutorial" chip, small video of a person bottom-right.
- "Quick start" docs card: tiny screenshot strip (`assets/quickstart-*.png`) + "Docs" chip. Copy: "Two minutes to get your Codex, Claude Codgent working in the browser for you. ego lite is a browser built for both you rome, it's based on Chromium, so you don't have to change a tu browse - your extensions," (sic — typos are in the original; keep meaning, we may normalize).
- Bottom row: 4 tiny colored icons (`icon-research/house/finance/career.png`) + link list "Analyze Competitors' Activity / Find Yourself the Best Job / Track Your Stocks in One Pass" + circular arrow "Learn more" button.

Page telemetry: PostHog (`page: "welcome_aboard"`) — **omit in Prism** (no third-party telemetry).

## 3. First-run onboarding (fresh profile only)

Captured live on a throwaway profile (sequence, dark-to-light blue gradient bg,
Kulim Park display font, `start-motion.css` transitions):

1. Splash: "Welcome to **ego** (𝘭𝘪𝘵𝘦)" wordmark top-left, glyph outline top-right, animated rotating agent name ("the browser you can share with *Claude Code* → Codex → …"), primary button "Get started →".
2. Pitch: Chrome window → ego window morph illustration, headline "Feels like Chrome. Nothing to relearn.", sub "ego (lite) keeps everything familiar, while helping you get things done better.", button "Continue".
3. Import wizard: "Import from another browser" / "Bring your bookmarks, history, and more into ego (lite), so it's ready to work for you from day one." — rows per detected browser (Chrome icon + "All profiles (4)" dropdown + checkbox), note "Each selected browser profile will be imported separately", footer "🔒 Your data stays on your device. We never collect it", buttons "Import" (primary) and "Skip" (ghost). → This is the visual target for our existing chrome-import flow.
4. Finish: primary "Open ego (lite)", checkboxes "Set as default" / "Add to Dock" (default on) / "Share crash reports" (default off).

Prism currently has **none** of this first-run flow — it needs to be built (gated on first run only).

## 4. Spaces overview (native UI)

Trigger: top-right "Open Space (⌥S)" button (30×30, title-bar area) or ⌥S.
Observed end state (screenshots, both user's and probe):

- Whole browser content scales down into a rounded live card (~0.42×, spring/ease-out animation) placed on a dark backdrop; the card remains a LIVE view of the space.
- Top center: "N Space(s) ⌄" caption (dropdown for space list).
- Left-bottom of each card: label "Space"; right-bottom: muted watermark "Your ego" (uses `ego-mark-muted.svg` style glyph).
- "+" card ("Create a new Space") to the right.
- Bottom hint bar (first runs): "Hold ⌥ and press S repeatedly to quick-switch Spaces."
- Selected space card gets a blue border.
- Clicking a card zooms back into that space (reverse animation). While in overview, the top-right button stays ("Open Space (⌥S)").
- The user's instance showed "2 Spaces" incl. a running background-agent space labeled with its task name and a "Running" chip — spaces are also the background-agent containers.

Our `prism_space_window_delegate` implements the basics; animation polish (spring curve,
backdrop dim, hint bar) is the gap.

## 5. Native chrome brand elements (user-flagged)

- Omnibox chip: `AXPopUpButton "ego (lite)"` 85×22 at left of URL field — ego mark + "ego (lite)" text. Prism must render the same chip with the LogiBricks mark + "Prism" text.
- Tab strip: ego tabs show the ego mark as the leading chip icon on agent/welcome tabs. Prism needs the LogiBricks mark there.
- Top-right: ego avatar button (`AXPopUpButton "ego"` 32×32, right of bookmark star) opens the agent menu; on the user's instance it shows a **count badge** (dark circle, white numeral = number of running background agents). Prism has the dock badge only — the toolbar button + count badge need to be replicated.

## 6. Branding deltas for Prism (do NOT ship ego marks)

- All visible ego glyphs/marks → LogiBricks mark (derive from `chromium/branding/icons/` pipeline; add small glyph variants: 16×10-ish nav mark, 78×48 welcome header, muted watermark, toolbar 20px).
- Strings: "ego (lite)" → "Prism", "ego://welcome" → "prism://welcome" (keep our existing chrome://prism-welcome alias), "/ego-browser" → "/prism-browser", `npx skills add citrolabs/ego-lite` → our skill install command.
- Kulim Park is OFL-licensed — safe to bundle as-is for the onboarding display font.
- Keep ego asset files only under `chromium/reference/` for parity checks; the shipped WebUI uses Prism-branded copies.

## 7. Follow-up recon (2026-08-28, from user's ego recordings/screenshots)

- **Spaces/dashboard trigger lives on the TAB STRIP row**, window top-right corner (AX: 30×30, "Open Space (⌥S)"), NOT on the toolbar row next to the avatar. When 0 agents run it shows a four-squares grid icon; when >0 it renders as a dark circle with a white numeral (the running-agents count badge). The toolbar-row avatar button is a separate agent/profile menu and carries NO badge.
- **Watermark is dynamic**: fresh profile shows "Your ego"; the user's signed-in machine shows "LogiBricks.AI Agents EGO" — i.e. `<account/display name> Agents EGO` with a "Your ego" fallback. Prism equivalent: `<display name> Agents Prism`, fallback "Your Prism".
- **Dashboard layout (1920×1080 reference)**: cards sit near the top (row starts ≈8% from top), uniform card size, ~40px gaps; selected/current space card has a blue border, others thin gray; under each card: left = "Space" label (agent cards: blue "Running" chip + task name), right = watermark; top center "N Spaces ⌄" dropdown; trailing "+" dashed card; bottom-center hint bar with keyboard icon ("Hold ⌥ and press S repeatedly to quick-switch Spaces."); corner badge stays visible while in dashboard; window traffic lights visible top-left.
- **Open/close animation (~0.5–0.7s, ease-out/spring)**: on trigger, the live page content lifts slightly (translateY) and scales down into its card slot with a crossfade; the other cards stagger/slide in from the right; the "N Spaces ⌄" caption and the hint bar fade in. The agent space card's live content pops in right after the transition settles. Reverse animation when a card is clicked to open that space.

## 8. Enter-animation frame analysis (2026-08-31, ego video vs Prism window-capture)

ego's enter animation (from the user's 1080p recording, frame-sampled):
1. The ENTIRE window content (page + browser chrome) lifts vertically (~5-10% of height) with a short motion ghost/crossfade.
2. The content scales down into the LEFT card slot while the dark dashboard backdrop fades in; the top row morphs (tab strip fades out as the "N Spaces ⌄" caption fades in) — no jump cut.
3. The other cards (agent space, "+") are present almost immediately with a subtle fade/rise — they do NOT travel far from the right.
4. Caption + hint bar fade in during the second half; the agent card's live content pops in after settle. Total ≈0.6-0.8s single ease-out.
5. The current browsing window is ALWAYS a card ("Space") — ego counts the implicit default browsing context as a space ("1 Space" with nothing else running).

Prism gaps found by window-capture (screencapture -l, 10fps):
- Entering from the default (non-agent) window shows "0 Spaces" and no shrink animation at all — the current content just disappears, because the implicit default space has no wall card. This is the user's #1 complaint.
- Chrome collapse (toolbar/tab strip hide) reads as a jump cut relative to the wall appearing.
- Non-current cards stagger in with too much right travel vs ego's subtle in-place fade.
Fix direction: default space becomes a first-class wall card ("Space", live thumbnail, counted in the caption); the FLIP lift+shrink runs from EVERY entry path; choreograph the top-row crossfade with the shrink; reduce non-current card travel to a subtle rise/fade with ~40-60ms stagger.
