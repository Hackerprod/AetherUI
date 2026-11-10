# Data-state source newlines

`HtmlParser::parse_text` normalizes source bytes within each ordinary text
interval before `html_decode`: contiguous CRLF becomes LF and every other CR
becomes LF. The rewrite is in place in the existing raw string, without another
allocation, parser state, or a pending CR carried between intervals or parses.

Markup and comments separate source bytes. Thus `aa\r\nbb` becomes `aa\nbb`,
while `<span>aa\r</span><span>\nbb</span>` contains two normalized LF characters.
The latter correctly has three lines in pre, pre-line and pre-wrap, not two.
At a 20px line height the expected height is 60px. Normal and nowrap still
collapse those newlines as whitespace.

Character references are decoded afterwards. `&#13;` and `&#xD;` retain CR;
`A\r&#10;B` becomes `A\n\nB`, and `A&#13;\nB` remains `A\r\nB`. Entity-created
markup remains text. CSS treatment of entity-created CR is a separate existing
contract and is not changed here.

This is deliberately not global HTML input preprocessing. RCDATA keeps its
existing normalization and textarea leading-LF behavior. RAWTEXT, comments,
attribute values and explicit DOM mutations retain their existing semantics.
The public input is a C string, so embedded NUL remains outside this change.

The new DOM tests check exact bytes, EOF, invalid tag openers, entity ordering,
UTF-8, source barriers, large input, and repeated document replacement. Geometry
and paint checks cover five white-space modes and both measurement callbacks.
Allocation probes fail the raw-string and decoder allocations and require an
uninjected retry with exact DOM bytes. They are not a full OOM sweep: the legacy
parser replaces the previous tree before parsing and can propagate `bad_alloc`;
this change does not add rollback or an ABI OOM guarantee. Layout OOM transaction
tests remain separate.

References:
- https://html.spec.whatwg.org/multipage/parsing.html#preprocessing-the-input-stream
- https://github.com/chromium/chromium/blob/main/third_party/blink/renderer/core/html/parser/input_stream_preprocessor.h
