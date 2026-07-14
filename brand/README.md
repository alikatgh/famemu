# Ember brand assets

The Ember identity — a glowing-ember mark that ties into the famemu
"warm-light" family (lantern → wick → ember). Hand-authored SVG (crisp at
any size, exact brand hexes, tiny), **not** AI-raster art.

## Files

| File | What it is |
|------|-----------|
| `ember-mark-8.svg` | Ember 8 glyph — a coarse glowing **coal**, 2 colors (few big pixels) |
| `ember-mark-16.svg` | Ember 16 glyph — a taller **flame** from a coal, 3 colors (finer pixels) |
| `wordmark.svg` | `ember` **pixel logotype** — path-locked, no font dependency |
| `lockup.svg` / `lockup-dark.svg` | mark + wordmark; light wordmark (dark bg) / dark wordmark (light bg) |
| `favicon.svg` | Platform favicon — the flame, drawn 16px-native |
| `icon-bg.svg` | Opaque flame on brand-dark — source for app/social icons |
| `og.svg` | 1200×630 social banner (flame + wordmark + 8·16 badges + tagline) |
| `png/` | Rendered exports (see below) |
| `preview.html` | Contact sheet: marks, lockups, favicon sizes, light/dark |
| `LOGO_BRIEF.md` | The original generation brief (kept for reference) |

### `png/` exports

`ember-flame-512`, `favicon-16/32/48` (transparent) · `apple-touch-icon`
(180), `icon-1024` (opaque brand-dark) · `lockup` / `lockup-dark`,
`wordmark` (transparent) · `og` (1200×630 social card).

Regenerated from the SVGs with headless Chrome (exact sizes, correct
transparency) — qlmanage/sips pad non-square SVGs, so Chrome is the
rasterizer of record:

    CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
    # wrap the .svg in an <img> at the target px size, then:
    "$CHROME" --headless=new --force-device-scale-factor=1 \
      --window-size=W,H --default-background-color=00000000 \
      --screenshot=png/out.png page.html

The two marks encode the hook **bit-depth = fire**: Ember 8 is a two-color
coal (fewer colors, like the 8-bit palette); Ember 16 is a three-color flame
with a bright core (more colors, like the 16-bit palette). Same family
silhouette, different richness.

## Color tokens (identical to the site's `style.css`)

| Role | Hex |
|------|-----|
| Ember orange (body) | `#FF7A45` |
| Deep red-orange (base/coal) | `#E8522A` |
| Molten gold (Ember 8 core) | `#E8BE3C` |
| Bright gold (Ember 16 hot core) | `#FFE9A8` |
| Background (charcoal-indigo) | `#0E0D16` |

## Wordmark & lockup

Wordmark: lowercase `ember` as a **pixel logotype** (`wordmark.svg`) — a
designed letterform locked to `<rect>` paths, so it's font-independent and
matches the pixel flame. A bit badge `8` (gold) or `16` (ember) is added
beside it per context. Lockup = flame mark + `ember` (`lockup.svg`); shared
mark, the badge is contextual. The live site header uses the flame mark +
`ember` set in mono HTML text (selectable/accessible); logo *assets* use the
pixel wordmark so they never depend on a font being installed.

## Usage everywhere

- **Website**: `site/favicon.svg` + header `.brandmark` (wired in).
- **GitHub**: use `ember-mark-16.svg` (the flame) as the repo social/README
  logo; the coal reads best only alongside the flame as a pair.
- **App icon / favicon PNGs**: rasterize `favicon.svg` to 16/32/48/180 px
  when a host needs PNG (SVG favicons already scale in modern browsers).
- Keep the two marks a **pair** in any "8 vs 16" context; use the flame
  alone as the single platform ember.

## Regenerating PNG exports (optional)

    # needs librsvg: brew install librsvg
    for s in 16 32 48 180 512; do
      rsvg-convert -w $s -h $s favicon.svg -o favicon-$s.png
    done
