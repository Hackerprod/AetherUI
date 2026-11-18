# B2 test layers

The tests deliberately separate parsed-DOM observations, public production behavior and private corruption tests. A CMake registration is not evidence of execution.

- `test_b2_inline_wrap.cpp`: public headers + production shared library; additive capability, input lifetime, owner thread, NO_FRAME/sequence, repeated render/copy, guard bytes/alpha, split/unsplit, whitespace across markup, mixed style, center, forced-line heights, direct flow contexts. Pinned DejaVu hmtx advances provide independent exact-width and epsilon oracles, including a markup-split NBSP word and visible overflow.
- `test_b2_inline_failures.cpp`: separate DOM/STYLE/TEXT/CSS-loss negatives, ineffective/empty descendants, href decoding, and reachable limits. Unsupported padding shorthand is a CSS-accounting rejection; supported padding longhand on inline is a STYLE rejection.
- `test_b2_parsed_dom.cpp`: observes this parser rather than assuming HTML5 browser repairs. It currently retains p>div, label>input and nested anchors; void input repairs an apparent child to a sibling. Admission tests reject only hierarchies actually present.
- `test_b2_inline_ledger.cpp`: direct counters at 0/limit/+1, checked arithmetic, PMR allocation failure/retry, UTF-8 and provenance corruptions. Unreachable public quota thresholds are tested directly, not represented as fictitious public inputs.
- `test_b2_inline_semantics.cpp`: private hook build only; mutates a real session ledger before final publication. Tests equal-string origin substitution, coverage, collapse, empty participants, br and style/line/baseline corruption, plus independent line/context expectations. Every corrupted creation must fail without a published session and a clean retry must succeed.
- `test_b2_repertoire.cpp`: all 256 Latin scalar values, malformed UTF-8, whitespace and unchanged legacy tokenizer behavior.
- Existing `test_b0_source_oom.cpp` also runs with `b2_inline_oom.html`: exhaustive ordinary/aligned allocation interposition across creation and render, cleanup/live-allocation accounting, output preservation and pixel-identical recovery. No production hook is needed.

B0/B1 captures must remain byte-identical. Existing inline unit, intrinsic, hanging, alignment, visibility, lifetime and OOM suites remain regression gates. Sanitized helper execution is not a substitute for a sanitized full-library public run. Leak checking blocked by an execution environment must be reported separately from ASan/UBSan.

The B2 fixtures are derived static feature examples, not the original login. PNG inspection is a layout sanity check, not a browser parity or performance claim. The original HTML is tested intact only as a remaining-blocker diagnostic.
