# HTML source newlines

`aether_doc_parse_html` preprocesses the complete C-string source before the
HTML tokenizer: contiguous CRLF becomes LF and every remaining CR becomes LF.
This applies to Data, RCDATA, RAWTEXT (script/style), comments, attributes and
markup delimiters. The Data and RCDATA paths no longer normalize separately.

Without CR, the parser uses the original pointer and length, with no new
normalization allocation or copy. The CR search still scans the source. With
CR, an owned contiguous string first copies the complete source, then compacts
in place from its first CR with forward read/write indices (write never exceeds
read). A final shrink establishes the normalized length and NUL terminator. The parser receives its actual
normalized length. The buffer lives until parsing finishes; DOM strings own
their contents and do not retain source views. Time is O(n), with O(n) additional
buffer space only on the CR path. This is not a streaming parser.

Markup and comments separate source bytes. Thus `aa\r\nbb` becomes `aa\nbb`,
while `<span>aa\r</span><span>\nbb</span>` contains two normalized LF characters.
The latter has three lines in pre, pre-line and pre-wrap, not two. At a 20px
line height the expected height is 60px. Normal and nowrap still collapse them.

Character references are decoded afterwards only in states that already
support decoding. `&#13;` and `&#xD;` retain CR in Data, RCDATA and attributes;
`A\r&#10;B` becomes `A\n\nB`, and `A&#13;\nB` remains `A\r\nB`. References in
comments and RAWTEXT remain literal. Decoded markup is never retokenized.
Textarea removes exactly its first decoded LF: an initial source CR/CRLF or
`&#10;` is omitted, while an initial `&#13;` is retained. Explicit DOM mutations
and CSS handling of entity-created CR are unchanged. The C-string boundary
still makes embedded NUL EOF; UTF-8 and other non-CR bytes are not rewritten.
This does not implement general HTML tokenizer conformance.

The byte tests cover all these states, quoted and unquoted attributes, source
barriers, entity ordering, EOF, end-tag suffixes, UTF-8/high bytes, large input,
reparse, and DOM reads after changing and releasing the input. Existing geometry
and paint checks cover five white-space modes and both measurement callbacks.

Allocation probes inject failure at three boundaries: the new source buffer,
raw text and reference decoding. The new source allocation is before document
touch/destruction, and its failure leaves the old DOM intact. Later allocations
retain the legacy nontransactional behavior and can propagate `bad_alloc`.
Each probe requires an uninjected retry with exact DOM bytes and removal of old
IDs. These are not a complete OOM sweep and do not establish an ABI OOM or global
rollback guarantee. Layout OOM transaction tests remain separate.

References:
- https://html.spec.whatwg.org/multipage/parsing.html#preprocessing-the-input-stream
- https://github.com/chromium/chromium/blob/main/third_party/blink/renderer/core/html/parser/input_stream_preprocessor.h
