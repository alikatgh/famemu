# Ember marketing site

The public-facing site for the Ember 8 / Ember 16 engines. Static, self-
contained, no build step — three files:

- `index.html` — the pitch (vision, the two consoles, how the store works).
- `state.html` — the **public engine-state page**: feature matrix and
  verification status for both consoles, including the one documented gap.
  This is the "some of the state must be public" surface; keep its numbers
  in sync with `ctest` (lockstep frame count, golden-screen count, gaps).
- `style.css` — shared house style (dark-default, theme toggle, hairline
  borders, tabular numbers), matching the lantern/wick sites.

## Publishing

Served as-is by any static host. For GitHub Pages, point Pages at this
directory (or copy it to the Pages branch); intended custom subdomain:
`ember.famemu.aulenor.com` (set a CNAME → the Pages host, then set the
domain in the repo's Pages settings — same flow as the lantern site).

## Keeping the state page honest

The state page makes checkable claims. When engine capability changes, update
`state.html` in the SAME commit — and only mark a row `verified` if a gate in
`ctest` actually proves it. The numbers today: 43 lockstep frames + 3
gameplay scenarios at 0.000%, 66 golden screens, 16/16 CI groups, 1 gap
(SPC7110-class decompression).

## Local preview

    cd site && python3 -m http.server 8731
    # open http://localhost:8731/
