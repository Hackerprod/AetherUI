# B1 verification contracts

`test_b1_semantic_page.cpp` is an external ABI consumer. It uses only installed
public headers and the production shared library. It reads both advertised B0
and B1 capability bits independently, without treating the current mask as a
closed enumeration.

The selector test is deliberately separate from semantic-to-div equivalence.
Its original DOM contains `section`, `div` and `aside` nodes and checks their
public tag identities. Its rendered samples verify distinct type selectors,
a `main aside` descendant selector, later same-specificity source order, and a
class selector winning over a later type selector. Expected colors are explicit
BGRA values at fixed coordinates. It does not rewrite selectors or HTML.

The frozen semantic equivalence fixture and div control must have identical CSS
bytes, specificity and source order. Only container names differ. Compare all
active BGRA bytes, including alpha, separately from buffer guards. The independent
primary semantic fixture includes an actual aside type selector. Removing only
viewport metadata must preserve its pixels. The public C11 source consumer also
checks caller-input lifetime, repeated render/copy, owner-thread errors, NO_FRAME,
sequence 1 and destination guard bytes.

Negative expectations are based on the parsed DOM, not apparent source nesting:

- `section` in head or a text leaf remains an element child and is rejected at DOM.
- Markup within title becomes raw text, not a section. It is accepted as inert
  metadata and produces the same pixels as the document without title.
- Markup within style is raw text, not a section. It fails CSS accounting.
- Input is void. An apparent section child becomes a body sibling and is accepted
  as that sibling. This is not support for children of input.

Viewport duplicates/conflicts, noncanonical values, mixed charset/name variants,
extra attributes, and body placement reject at DOM. Existing unsupported layout,
effects, typography, inline elements and control types remain negative cases.
Unused and metadata-targeting CSS loss and inline loss must not disappear.

The existing exhaustive allocation-interposition consumer is registered twice:
B0's unchanged fixture and B1's primary fixture. It checks failed-create cleanup,
accepted recoveries for identical pixels, render failure with NO_FRAME, retry and
copy failure output preservation. CMake registration is not evidence CMake ran;
report direct compiler execution, CMake, and sanitizers separately.
