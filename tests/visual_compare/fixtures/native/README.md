# Native profile capture fixtures

These new fixtures are intentionally limited to the public HTML LTR profiles.
`profile1-transparent.html` uses profile 1 and includes text on transparent pixels.
`profile2-static-controls.html` uses profile 2 with real static input/button tags.
Neither is a functional login, an interaction test, a Chromium comparison, or a
claim of general HTML/CSS support. Original negative fixtures are unchanged.
The files intentionally end at `</html>` because the strict profile rejects
trailing bytes; capture passes the exact verified HTML bytes without rewriting.

Use viewport 480x320, scale 1, `--regular-font fonts/DejaVuSans.ttf` and
`--bold-font fonts/DejaVuSans-Bold.ttf`, resource root this directory, and
`resources.json`. Fonts are unmodified copies of the repository's existing
`native/tests/fixtures/fonts` test resources. Their original provenance and
redistribution license are included in `fonts/README.md` and
`fonts/LICENSE-DejaVu.txt`. The manifest inventories only consumed font files,
not license/documentation files, which are not render resources.
