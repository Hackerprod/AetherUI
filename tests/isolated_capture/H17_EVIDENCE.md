# H17: first-of-type in the admitted HTML selector model

The native selector parser records first-of-type as a structural dependency.
The matcher scans preceding DOM element siblings for the same HTML type using
the existing ASCII-insensitive type identity. Text, comments, generated content
and anonymous layout nodes are not type siblings; hidden elements count.
Root and detached elements use the same DOM boundary rules. This does not
implement namespace-aware XML/SVG identity or claim those capture profiles.

H11 deliberately does not add this pseudo to its no-match proof whitelist:
it retains the full canvas restriction rather than assume the additional
sibling scan has been budgeted. H5/H15 use the existing strict native parser;
there is no harness-specific selector matcher.

Commands:
- sh tests/isolated_capture/build.sh /tmp/h17-clean-20261113
- sh tests/isolated_capture/run_h17_core.sh /tmp/h17-clean-20261113
- python3 tests/isolated_capture/test_h17.py /tmp/h17-clean-20261113/aether_capture /tmp/h17-pixels2

Clean build: 71 core + 8 extras. 1,714 native checks, 12 parser OOM points and
163 transactional runtime OOM points pass. Mutation tests move the first
same-type sibling to the end, move it between parents, remove and reattach,
and compare retained runtime updates to fresh full resolution after each step.
They check both the sibling losing and gaining the match. Parser tests cover
escaped names, case, composition, specificity through pixels, mixed tags,
classes, leading text/comments, hidden siblings, root, detached elements and
malformed arguments.

21 CLI checks (14 captures and seven exact PNG comparisons) pass. PNG inspected:
first div is red although preceded by an i; other items retain blue. All native
H3-H16 and CLI regressions pass; logs /tmp/h17-native-regression.log and
/tmp/h17-cli-regression.log. Runtime preparation OOM preserves the old revision.

The original fixture is unchanged (SHA-256
eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837).
Its previous first-of-type structural rejection is removed. The next canvas
analysis failure is display at CSS bytes [5147,5163), selector [5119,5146).
The first global function rejection remains [21716,21728). These are partial
diagnostics, not proof of a complete engine inventory. The original remains
rejected; no Chromium execution or full visual/performance parity is claimed.

Reference: Selectors Level 4 first-of-type and Chromium SelectorChecker, as
reviewed in the proposal. No external source was copied.
