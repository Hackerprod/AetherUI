# H20 property/value admission contract

This is an authored-source gate, not a general CSS validator. Admission is the
intersection of the bounded capture profile and native consumers in
`native/src/style_runtime/builtin_properties.cpp`; native parser acceptance
alone is NOT sufficient proof of CSS semantics. Resolved-style and paint guards
remain mandatory. Passing this table does not promise that a scene is rendered.

All declarations are checked, including overridden, unmatched and inactive rules.
Custom property definitions are data; substituted consumers are checked. Names
are closed, keywords literal lowercase, numeric parsing uses the native complete
literal parser. Escapes, slash/comment-dependent spellings, percentages and
numeric functions are excluded. Float-backed numbers are zero or magnitude
1e-6 through 65536. Lengths are px or unitless zero. No implicit CSS-wide keyword
support: only the explicitly listed keywords below are admitted.

| Property family | Admitted value domain | Consumer / intentional exclusions |
| --- | --- | --- |
| direction | ltr rtl initial inherit unset | direction consumer; no revert |
| display | block flex none initial inherit unset | display consumer; other layout modes excluded |
| flex-direction | row row-reverse column column-reverse initial inherit unset | physical flex flows |
| flex-wrap | nowrap wrap initial | wrap-reverse excluded |
| justify-content | start end flex-start flex-end center space-between initial inherit unset | native space-around/evenly approximate center, therefore excluded |
| align-items | start end flex-start flex-end center stretch initial | native baseline approximation excluded |
| align-self | previous row plus auto | auto resolves from parent |
| align-content | stretch initial | other native ignored values excluded |
| position | static initial | positioned subtree outside RTL proof |
| overflow / overflow-x / overflow-y | visible initial | clipping/scroll behavior excluded |
| box-sizing | content-box border-box | literal enum |
| width / height / flex-basis | nonnegative length, auto | length-or-auto; basis content excluded |
| min-width / min-height | nonnegative length ONLY | `bi_try_non_negative_length` does not implement CSS auto; reject rather than silently retain prior bound |
| max-width / max-height | nonnegative length, none | `bi_try_max_size` accepts non-CSS auto; gate deliberately rejects it |
| flex-grow / flex-shrink | nonnegative scalar | bounded factors |
| order | signed decimal integer in int32 range | exponent/fraction excluded |
| flex | none, auto, scalar, or grow shrink basis | three-term basis length/auto; two-term forms excluded |
| gap | one/two nonnegative lengths | normal and percentages excluded |
| row-gap / column-gap | one nonnegative length | no automatic shared size grammar |
| physical margin sides / margin | signed length or auto; one / 1–4 terms | physical only |
| physical padding sides / padding | nonnegative length; one / 1–4 terms | auto excluded |
| background / background-color / color | transparent black white red green blue yellow, #rgb, #rrggbb | no image/function/named-color expansion |
| border / physical border side | width style color in that order | exactly 3 terms; style solid/none/hidden |
| border-width/style/color and physical sides | length / solid-none-hidden / color; 1–4 or one term | no named widths, alternate ordering, or unsupported stroke styles |

Unknown names, vendor aliases and logical properties fail closed. A future
expansion must establish CSS grammar, actual native consumption, cascade behavior,
resolved guards and raster evidence together; do not widen a shared helper just
because a related property's parser accepts the token.

## Regression strategy

`test_h20.py` tests all seven size properties against ten keyword classes plus
numeric forms. Earlier valid declarations expose ignored values; following valid
declarations isolate authored admission from final resolved restrictions. The
four min/max properties additionally cover important, unmatched, inactive,
overridden and substituted `auto`. Existing matrices cover every numeric family,
aliases, escapes, shorthands and physical RTL placement. This documents bounded
coverage, not full CSS conformance or browser differential equivalence.
