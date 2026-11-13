# T1.4: explicit internal measurement contract

This phase does not ship a shaping provider or enable HTML text capture. The C ABI
and default callback behavior remain unchanged. Real HarfBuzz/FreeType shaping,
font parsing, contextual shaping, rasterization, original Flexbox PNG, Chromium
comparison and performance parity remain NOT_RUN.

## Selection and transaction

The internal engine retains an immutable MeasurementSelection. Null selection is
legacy. A native selection binds an explicitly supplied immutable FontInstance
and an owned provider; it does not resolve CSS family names or substitute fonts.
The caller must bind the intended font, face and size. A different requested size,
missing resource, callback/native conflict, null run, wrong source/font or divergent
metrics fails. Legacy mode carrying native resources also fails. There is no
fallback to callback, zero width or a generated mask.

Selection mutation is rejected throughout layout. The contextual callback setter
also refuses reentrant mutation in native mode; legacy behavior is unchanged. Engine access remains single-owner-thread, not a
new thread-safe API. Providers must keep mutable future HB/FT handles on their owner
thread and return immutable value runs only. Runs retain their fonts and bytes
after the provider/configuration is gone.

Intrinsic tokens and final fragments use the same transport boundary. This proves
selection consistency, NOT contextual shaping equivalence: their text grouping can
differ, affecting kerning and ligatures even in Latin. No supported real-text
profile is claimed. Opaque form controls are rejected in native mode.

Native exceptions are caught by the existing layout transaction. OOM returns OOM;
other invalid results return INVALID_STATE. A failed layout leaves the caller's
previous result untouched.

## Empty and spaces

Empty input is no text: zero metrics, no run, no provider call. Empty ShapedRun
sources remain invalid. Only a nonempty source of ASCII spaces may have no glyphs;
its explicit advance remains in its measured width. Other strings cannot masquerade
as successful missing-glyph runs. Existing snapshot/preparation retain the run;
the explicit A8 renderer's zero-iteration glyph loop paints nothing and needs no
mask for that run. The normal renderer still refuses Text. Real space glyphs with
empty coverage can instead be represented by actual provider glyphs/masks later.
No fake glyph is inserted to satisfy transport invariants.

## Font identity

FontSource owns bounded bytes and computes SHA256 once over those retained bytes.
read_exact reads one declared bounded source and rejects truncation or trailing
bytes. No reopen, path hash or label hash is used. Instances share FontSource
across sizes/runs; no layout token reloads or hashes it. Compatibility constructors
treat old labels as provenance, not trusted hashes. Digest proves byte identity,
not a valid font, authentic origin or license. No font substitution is performed.

## Tests

run_t14_font_source.sh checks FIPS vectors, corruption, truncation/trailing bytes,
bounds, sharing/lifetime, and the space invariant. run_t14_provider.sh uses an
explicit TEST-ONLY double: selection, validation, transaction, injected provider
OOM, reentrant setter refusal and retained resource lifetime. It is not a shaping
or font parser test. Existing transport, snapshot and A8 tests remain synthetic.
SDK headers/pkg-config are absent in the current environment; this phase installs
nothing and bypasses no SDK or capture gate.

The spaces regression also traverses snapshot/preparation/explicit A8 with no masks,
checking transparent and opaque backgrounds remain unchanged and ordinary render
still refuses Text.

The provider call is guarded like the legacy callback: a pinned document epoch and
node identity are checked immediately before and after it. DOM mutation during
measurement is rejected before layout resumes. Unknown selector modes are rejected
at construction rather than implicitly choosing legacy.
