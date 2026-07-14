# Ember brand assets

The Ember identity — a glowing-ember mark that ties into the famemu
"warm-light" family (lantern → wick → ember). Hand-authored SVG (crisp at
any size, exact brand hexes, tiny), **not** AI-raster art.

## Files

| File | What it is |
|------|-----------|
| `ember-mark-8.svg` | Ember 8 glyph — a coarse glowing **coal**, 2 colors (few big pixels) |
| `ember-mark-16.svg` | Ember 16 glyph — a taller **flame** from a coal, 3 colors (finer pixels) |
| `favicon.svg` | Platform favicon — the flame, drawn 16px-native |
| `preview.html` | Contact sheet: marks, lockups, favicon sizes, light/dark |
| `LOGO_BRIEF.md` | The original generation brief (kept for reference) |

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

Wordmark: lowercase `ember` in a mono / chunky geometric face; a bit badge
`8` (gold) or `16` (orange) beside it. Lockup = `[mark] ember 8` /
`[mark] ember 16` — shared mark, badge changes. The site header uses the
flame mark + `ember` at 20px (`.brandmark` in `site/style.css`).

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
