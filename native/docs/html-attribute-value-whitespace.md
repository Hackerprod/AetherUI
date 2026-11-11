# HTML whitespace in attribute values

Attribute value parsing uses the existing ASCII HTML whitespace predicate for
exactly two decisions: skipping whitespace immediately after `=`, and ending
an unquoted value. The set is TAB, LF, FF, CR and SPACE, independent of the C
locale at these decisions. Literal VT (U+000B, byte 0x0B) starts or continues
unquoted data and is preserved, including at the beginning or end of the value.
The unsigned-byte conversion makes these decisions independent of plain-char
signedness. Other parser classifiers and name handling are unchanged; this is
not a claim of locale independence or HTML conformance for the whole parser.

This deliberately retires the VT divergence preserved by the slash correction
`4453fddbbb31e2da70b150194c8297116d2102e7`. It explicitly supersedes the historical
`std::isspace`/VT limitation in the **HTML attribute values** section of
`native/README.md`, for attribute values only. README was not updated in this
cycle. Its earlier compatibility decision was intentional, and the original
regression input is retained with the new expected full map:

- `<input id="t" data-route=x\vdisabled>` has `id=t` and
  `data-route=x\vdisabled`, with no `disabled` attribute (`\v` denotes byte 0x0B).
- Content depending on VT as a separator can now produce different values,
  IDs, class tokens or boolean state. There is no assertion that no consumer
  depended on the former behavior. Use actual HTML whitespace to separate
  attributes intentionally.

A leading VT selects the unquoted branch even if followed by a quote. Thus
`data-v=\v"a>` publishes the value bytes `0b 22 61`; the paired input ending
before `>` remains an incomplete opening and is not published. Quotes after
unquoted data do not group subsequent whitespace. Slash inside an unquoted
value remains data, including directly before `>`. Whitespace followed by `/>`
keeps the existing outer tag behavior; there is no new self-closing correction.

Source CR and CRLF normalization still occurs before parsing. Reference decoding
still occurs once after the source value is read. Reference-created CR, VT,
whitespace, quotes, slash and `>` remain value bytes and are never retokenized.
For example `a&#13;b` and `a&#xD;b` both retain `61 0d 62`; `a&amp;#11;b` retains
literal `a&#11;b`. Direct DOM setters do not parse or normalize their values.
Duplicates still keep the first canonical occurrence while consuming later
values fully. EOF staging, names, tag/end-tag recovery, outer separators,
RCDATA, RAWTEXT and the `pre`/`listing` leading-LF rule are unchanged.

The implementation moves the existing initial whitespace scan into the value
reader and changes its predicate, then uses that predicate in the existing
unquoted scan. It adds no public API, dependency, buffer, allocation, persistent
state or extra traversal. Complexity stays linear. This is a semantic correction,
not a measured speedup.

## Focused checks

`aether.html-attribute-value-whitespace` uses only the public C ABI and keeps all
checks active with `NDEBUG`. It covers literal and decoded bytes, both quote
forms, true whitespace/CRLF, EOF publication, slash, duplicate maps, UTF-8/NBSP/
NEL, exact ID/class tokens, setters, source mutation/destruction, reparse and a
4-KiB value. The synthetic integration checks `input[disabled]` and computed
width 19 versus 73px. It does not infer layout geometry or paint from that width.
The previous unquoted corpus is retained with only the identified VT expectation
updated. Companion regressions and installed-package checks are intentionally
selected by exact name, not the entire native suite.

The helper's separate signed/unsigned-char tests do not establish parser-core
coverage under both flags. Such a claim requires two actual core builds and
installed consumers. Sanitizer results likewise require their own instrumented
core and fixture, not an ordinary Release run.

## References

- WHATWG: [before attribute value](https://html.spec.whatwg.org/multipage/parsing.html#before-attribute-value-state),
  [unquoted attribute value](https://html.spec.whatwg.org/multipage/parsing.html#attribute-value-(unquoted)-state),
  [source preprocessing](https://html.spec.whatwg.org/multipage/parsing.html#preprocessing-the-input-stream).
- Chromium source at `7fe04cd3b6456bd7baec869f6a5efaebd6e79b71`:
  [HTML tokenizer](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/html/parser/html_tokenizer.cc#L926-L1046)
  and [whitespace predicate](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/html/parser/markup_tokenizer_inlines.h#L39-L42).
  This is reference-source agreement, not browser-execution evidence.
