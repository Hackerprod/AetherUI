# Initial LF in `pre` and `listing`

The Data parser suppresses exactly one initial LF after a complete, exact
ASCII-case-insensitive `pre` or `listing` opening that actually opens insertion
context. The source name must have the existing HTML boundary (HTML whitespace,
`/` or `>`). A truncated name such as `pre+foo` or a VT boundary does not qualify.
The simplified non-void self-closing behavior is preserved: `<pre />` does not
open context, while `/` in an unquoted attribute value remains data.

A private boolean in the parser represents a single opportunity. The first
character run consumes it after the existing source CR/CRLF preprocessing and
reference decoder. Only a first byte equal to LF is removed; all other bytes,
including a second LF or CR from `&#13;`, remain. No second decoding occurs and
no empty text node is created when that LF was the entire run.

A non-character token cancels the opportunity even when the tree discards that
token. This includes comments, declarations, processing instructions, child
openings and ignored named closes. Invalid `<` is retained as text and consumes
the opportunity. The exact source sequence `</>` emits no token: it advances
three bytes without consuming or rearming the boolean, including repetitions.
`</ >` and `</\t>` retain their existing simplified representation and cancel it.
For example, `</>\nA` becomes `A`, but `A</>\nB` stays `A\nB` and
`</><!--x-->\nA` retains the comment and LF.

A nested eligible opening replaces the parent's opportunity. Parser construction
resets it for each parse, including reparses of the same document and EOF after
an opening. There is no state on DOM nodes, in layout, in painting or globally.
Moving existing text into a preformatted element with `aether_node_append_child`
does not suppress a newline. RCDATA (`textarea`/`title`), RAWTEXT and ordinary
Data elements retain their existing handling.

The change adds no public API, allocation, auxiliary buffer or tree traversal.
Erasing a leading byte can move the remaining decoded string once. This is a
semantic correction, not a performance claim. Public `id` attributes and lookup
are preserved. Internal sequential style IDs need not have the same numeric
values as before when an entire LF text node is omitted; IDs remain coherent
and unique within the resulting document.

## Focused regression gates

`aether.html-pre-leading-lf` retains the 91 approved DOM examples and adds bounded
structural, EOF/reparse, token boundary, reference, mutation and 64-KiB byte
controls. All checks run under `NDEBUG` and any failure returns nonzero.

`aether.html-pre-leading-lf-consumers` covers 40 pairs: both element names, legacy
and contextual text measurement, one/two leading LF, and `pre`, `normal`,
`nowrap`, `pre-line`, `pre-wrap`. It checks literal DOM bytes, text painting,
rectangles and relayout. The independent control is a neutral `div` with the
literal expected bytes and identical explicit CSS, viewport and callbacks.
Expected bytes must not be reparsed under `pre`/`listing`, which would suppress
again. With `white-space:pre;line-height:20px`, one LF gives 20px height and
`A` at y=0; two LF give 40px and y=20 (0.01px tolerance). Other CSS modes are
compared with their matching neutral control, not assumed to share geometry.

The companion HTML regression names are explicitly limited to:
`aether.html-data-newlines`, `aether.html-source-newlines`, `aether.html-rcdata`,
`aether.html-raw-text`, `aether.html-comments`, `aether.html-end-tags`,
`aether.html-tag-open`, `aether.html-tag-names`, `aether.html-start-tag-eof`,
`aether.html-unquoted-attributes`, and `aether.html-numeric-references`.
This contract does not imply full HTML tokenizer/tree-builder conformance,
new named entities, changed UA defaults, or a general self-closing correction.

## References

- WHATWG HTML, [in body](https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-inbody),
  [end tag open](https://html.spec.whatwg.org/multipage/parsing.html#end-tag-open-state),
  and [input preprocessing](https://html.spec.whatwg.org/multipage/parsing.html#preprocessing-the-input-stream).
- Blink `HTMLTreeBuilder`, observed Git blob `4d1fb8df055edea00eefdb16482e2b2442bd128d`,
  lines 226–231, 549–562, 972–978, 2696–2713: arm, cancellation and one-character LF handling.
- Blink `HTMLTokenizer`, observed Git blob `ae05b047383173cdf3bf88ac82e7a697f958fc2e`,
  lines 379–388: the exact empty end-tag spelling returns to Data without emitting a token.
