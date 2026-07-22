# CLAUDE.md

Fork of hyprnux/hyprglass, a Hyprland plugin (Apple-style Liquid Glass effect).
Origin remote is `KeesTucker/hyprglass` — this fork's `main` IS the patched
code, targeting whatever Hyprland version is currently installed (see
`.hyprland-version`).

## Dev workflow

Two parallel ways this plugin gets loaded, for different purposes:

- **Local iteration**: `make` then `hyprctl plugin load $(pwd)/hyprglass.so`
  / `hyprctl plugin unload ...`. Fast, no hyprpm overhead. Use this while
  actively editing source.
- **Stable/persistent**: managed by `hyprpm` against this fork's GitHub
  remote (`hyprpm add https://github.com/KeesTucker/hyprglass`, `hyprpm
  enable hyprglass`). `hyprpm.toml` has no commit pins on purpose — it always
  builds `main` HEAD. Hyprland doesn't auto-load hyprpm plugins itself, so
  `~/.config/hypr/autostart.conf` has `exec-once = hyprpm reload -n` to load
  it every session.
- The plugin has a single-instance guard (env marker) — loading it manually
  while the hyprpm-managed copy is already active fails with "already
  loaded"; that's expected, not a bug.
- To ship a source change for the hyprpm path: commit + push to
  `origin/main`, then run `hyprpm update`. That command needs an interactive
  `sudo` prompt the first time (to install `hyprland-headers`), so it can't
  be run from a non-interactive/headless shell.

## User's Hyprland config (outside this repo)

Lives at `~/.config/hypr/`, Omarchy-based. Plugin config is
`~/.config/hypr/hyprglass.conf` (sourced from `hyprland.conf`), kept as an
explicit list of every tweakable setting at its current default — see
`src/BuiltInPresets.hpp` for the source of truth, since the README's defaults
table can lag behind live tuning commits (blur/refraction/aberration have all
been walked down by live user preference — check the comments in
`BuiltInPresets.hpp` before trusting a value from anywhere else).
`looknfeel.conf` disables Hyprland's own blur/shadow for perf on an Intel UHD
620 @ 4K; hyprglass does its own independent blur/shadow handling so this
doesn't conflict, but the effect is still a meaningful GPU cost on that
hardware worth watching.

## Architecture notes worth knowing before touching layer rendering

- `GlassLayerSurface::sampleAndRedirect` / `compositeAndRestore` implement a
  two-phase hook around Hyprland's `renderLayer`: sample+blur background and
  redirect to a temp FBO before the real render, then composite after.
- The SDF/refraction geometry (`fullSize`, `getCornerSDF` in `Shaders.hpp`)
  is driven by an explicit box, not the alpha mask — the mask only decides
  discard/composite. Layers that report oversized bounds (e.g. click-catching
  launchers like walker, which cover the whole output but only draw a small
  centered panel) used to get a flat, undistorted look, since the "glass
  edge" sat at the invisible reported boundary instead of the real content's
  edge.
- Fixed via `GlassRenderer::computeAlphaContentBox`: a per-frame tight alpha
  bbox (small GPU downsample + one CPU readback), computed in
  `compositeAndRestore` *after* the real surface render for that frame — zero
  lag, since the frame's content is already in the temp FBO by then. Feeds
  the SDF/draw geometry; the background sample's UV mapping is remapped via
  `narrowSampleLayout` to address this tighter sub-rect of the already
  larger, already-captured sample texture.
- `untransformedLayerBox` (inverse of `transformedLayerBox`) is unverified on
  real rotated-monitor hardware — mirrors the existing
  transform+invertTransform+dimension pairing exactly, just reversed, but has
  no test coverage on actual rotated hardware.
- Planned next step, for arbitrary (non-rectangular) shapes — e.g. a curved
  quickshell bar: replace the box SDF with a true per-pixel distance field via
  Jump Flooding Algorithm (JFA) on the alpha mask, and switch `refractionDir`
  from "toward box center" to the JFA gradient / nearest-boundary direction.
  Meaningfully bigger scope than the bbox fix — new multi-pass FBOs plus a
  refraction-direction rewrite, not a small follow-on.
