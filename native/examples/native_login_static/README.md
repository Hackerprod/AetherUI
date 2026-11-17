# Native static login, profile 2 (ABI 0.37.0)

Real input text/password and button elements, rendered entirely by the C++ core.
The C11 consumer only supplies fixture HTML and licensed font bytes, then copies
640×400 premultiplied BGRA. `login.html` is the separate static fixture; the
profile-1 card and original target page are unchanged. Values are examples only.

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic main.c -I../../include \
  -L/path/to/lib -Wl,-rpath,/path/to/lib -laether_core -o native-login-static
./native-login-static ../../tests/fixtures/fonts/DejaVuSans.ttf \
  ../../tests/fixtures/fonts/DejaVuSans-Bold.ttf login.bgra
```

## Scope matrix

| Requirement | Status in this fixture |
|---|---|
| HTML/CSS/layout/font measurement/masks/clip in C++ | R3.0 static |
| Text value, empty value placeholder, password bullets, button label | R3.0 static |
| Keyboard input, focus, caret, button activation | Pending R3.1 |
| Accessibility, labels/for, autofill, IME, selection | Outside profile 2 |
| Authentication, real credentials, network | Outside scope |
| General HTML/CSS, gradients, shadows, transforms | Rejected |

Profile 2 limitations are exactly 15; profile 1 stays exactly 3. No claim of
interactive login or browser fidelity. Password masking is not secure erasure:
source HTML/DOM exist during the operation. The provider receives presentation
bullets, not the source value. The final frame is committed atomically.

The public matrix is `tests/test_native_html_static_controls.c`; `--g1` selects
the initial input/empty/clipping/lifetime checkpoint. Build and test execution
results belong to the delivery evidence, not this scope description.

## Closed input contract

Profile 2 accepts the profile-1 block grammar plus `input type="text"`,
`input type="password"` and plain nonempty `button type="button"`. Input is void,
not self-closing. Control attributes use double quotes and exact `name="value"`
syntax, separated by ASCII spaces. Unknown well-formed types/attributes are
UNSUPPORTED; duplicate attributes, absent type and malformed syntax are
INVALID_ARGUMENT. Inputs accept only type, value, placeholder and style;
buttons only type and style.

Value/placeholder preserve spaces exactly. All original strings, including
hidden placeholder and password value, must be strict UTF-8 in the admitted
ASCII/Latin-1 repertoire. Entities, authored bullet, emoji and line controls
are rejected. Password uses one generated U+2022 per admitted scalar, requiring
the glyph in the selected regular/bold font. Placeholder uses fixed gray.

Maximum 32 controls, 256 scalars per value/placeholder, and 4096 authored
scalars in aggregate including hidden strings; original profile-1 quotas remain.
Input text is measured at its complete natural width and clipped to its content
box. Borders/background are outside that clip; no truncation or host drawing.
Profile 1 still reports limitations 3, profile 2 reports 15, unknown reports
UINT32_MAX. No new capability bit or changed options structure.
