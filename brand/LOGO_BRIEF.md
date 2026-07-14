# Ember — logo generation brief (for Gemini)

Paste this whole file into Gemini's image generator, or copy a single
**Prompt** block below. This brief creates the two logo marks for **Ember 8**
and **Ember 16**, the famemu platform's 8-bit and 16-bit game engines.

> **Important — generate the MARK only, no text.** AI tools garble letters and
> tiny numbers. Get the glowing-ember symbol from Gemini, then set the word
> "ember" and the "8" / "16" badge in real type afterward (see *Wordmark* at
> the end). Export the winners into this same `brand/` folder.

---

## The idea (why these marks)

The famemu family is a family of **warm-light** brands — *lantern*, *wick*,
and now *ember*. So the logo is a **glowing ember / small flame**. That ties
Ember to its siblings and looks nothing like any historical console wordmark,
which is the whole point: this is our own identity.

The memorable hook that tells the two apart is **bit-depth = fire**:

- **Ember 8** — a small, coarse, **two-color** coal. Fewer, bigger pixels.
- **Ember 16** — a taller, radiant flame, **three colors** with a bright core.
  More, smaller pixels.

That mirrors the real hardware difference (8-bit = fewer colors, 16-bit =
more), so the family reads as intentional, not decorative.

## Palette (locked to the website — do not change)

| Role | Hex | Used in |
|------|-----|---------|
| Ember orange | `#FF7A45` | both marks, main body |
| Deep red-orange base | `#E8522A` | Ember 16 base |
| Molten gold core | `#E8BE3C` | Ember 8 core / accent |
| Bright gold highlight | `#FFE9A8` | Ember 16 hot core |
| Background (charcoal-indigo) | `#0E0D16` | canvas behind both |
| Cool indigo glow | `#1A1928` | faint backdrop glow, Ember 16 only |

## Hard constraints (apply to every prompt)

Flat pixel-art / vector icon. Crisp hard edges, high contrast, centered,
generous margin, symmetrical, one clear silhouette that survives at **16×16
pixels** (favicon).

**Do NOT include:** any text, letters, or numbers; controller buttons, a
D-pad, or joystick; cartridge shapes; realistic or photographic fire; smoke;
gradient banding; 3-D, bevels, or drop shadows; thin lines or fine detail
that disappears when shrunk.

---

## Prompt 1 — the family sheet (use this first)

```
Two matching minimalist logo marks for a retro game-console brand called
"Ember", shown side by side on one solid dark charcoal-indigo background,
hex #0E0D16. Both marks depict a glowing ember — a small stylized flame
rising from a hot coal — built from chunky square pixels. Flat vector
pixel-art icon style: crisp hard edges, high contrast, no gradient banding,
no 3D, no bevels, no drop shadows, centered, generous margin, symmetrical,
iconic.

LEFT mark, the 8-bit version: a small, compact ember made of a FEW large
bold pixels, two warm colors only — deep orange body (#FF7A45) with a
molten gold core (#E8BE3C). Coarse, simple, unmistakable when tiny.

RIGHT mark, the 16-bit version: a taller, richer flame rising from the same
coal, made of MORE, smaller pixels, three colors — deep red-orange base
(#E8522A) blending up through orange (#FF7A45) into a bright white-gold core
(#FFE9A8), with a faint cool indigo glow behind it. More radiant and
detailed, but clearly the same family silhouette as the left mark.

Design goals: memorable, ownable, works as an app icon and as a 16-pixel
favicon. Generate 4 variations.

Do NOT include: any text, letters, or numbers; controller buttons or a
D-pad; cartridge shapes; realistic or photographic fire; smoke; thin lines
or fine detail that vanishes when shrunk.
```

## Prompt 2 — Ember 8 alone (refine after picking a direction)

```
A single flat pixel-art logo mark: a glowing ember / hot coal for an 8-bit
retro game console called "Ember 8". Built from a FEW large chunky square
pixels, two warm colors only — deep orange (#FF7A45) with a molten gold core
(#E8BE3C) — on a solid dark charcoal-indigo background (#0E0D16). Bold,
coarse, geometric, high contrast, centered, lots of margin. Must read as one
clear silhouette at 16x16 pixels. Flat vector, no gradients, no 3D, no
shadows, no text, no numbers, no controller motifs, no realistic fire.
Generate 4 variations.
```

## Prompt 3 — Ember 16 alone

```
A single flat pixel-art logo mark: a tall glowing flame rising from a hot
coal, for a 16-bit retro game console called "Ember 16". Built from MANY
small square pixels, three colors — deep red-orange base (#E8522A) up through
orange (#FF7A45) into a bright white-gold core (#FFE9A8) — with a faint cool
indigo glow behind it, on a solid dark charcoal-indigo background (#0E0D16).
Radiant, refined, geometric, high contrast, centered, generous margin. The
same visual family as a smaller two-color ember mark. Flat vector, no
gradient bleed, no 3D, no shadows, no text, no numbers, no controller motifs,
no realistic fire. Generate 4 variations.
```

## Alternate direction (swap in if the flame reads too generic)

Replace *"a small stylized flame rising from a hot coal"* with
*"a single glowing pixel-cube / hot coal, a chunky square with a radiant
hot core"* — even more bit-native and distinctive. Everything else (colors,
constraints, 8-vs-16 differentiator) stays the same.

---

## Wordmark & lockup (do this in a vector editor, not in Gemini)

- **Wordmark:** lowercase `ember` in a mono / chunky geometric typeface, to
  match the existing site brand voice. Keep it lowercase.
- **Bit badge:** a small `8` or `16` set in the same face, in ember orange
  (`#FF7A45`) or gold (`#E8BE3C`), tucked beside the mark.
- **Lockup:** `[ember-mark]  ember 8` and `[ember-mark]  ember 16` — the mark
  is shared; only the badge changes. This is what goes in the GitHub README,
  the site header, and the app.
- **Favicon:** export the mark alone (no wordmark) as a 16/32/48px PNG set
  plus an SVG. It already has to survive at 16px by design.

## Where the finished files go

Export the winners into this folder:

```
brand/
  LOGO_BRIEF.md      (this file)
  ember-mark.svg     (shared glyph, textless)
  ember8.svg         (mark + "ember 8" lockup)
  ember16.svg        (mark + "ember 16" lockup)
  favicon.svg        (mark only, 16px-safe)
  favicon-32.png  favicon-48.png  favicon-180.png (apple-touch)
```

Then swap the placeholder favicons in `site/index.html` and
`site/state.html`, and drop the lockups into the repo README and the site
header.
