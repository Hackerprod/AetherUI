# Drawing recovery checkpoint — 2026-11-16

This branch is a recovery checkpoint, not a release or a merge recommendation.
It preserves the recovered native source and tests plus the experimental image
interpolation correction, without rewriting the earlier backup history.

The image helper is `native/src/rendering/software/image_exact_lattice.h`.
It applies to a deliberately limited numeric domain; ineligible cases retain
the legacy renderer. Independent validation confirmed that the focused
fractional A/B examples improved from 18 differing channels each to zero,
while exact examples remained exact. The broader preserved discrepancy corpus
improved from 233 to 3 differences, with no newly observed differences: the
remaining alpha discrepancy is one I06 pixel repeated through three routes
with two curved clips, outside the helper's domain. This remains a failure,
not an accepted tolerance. Full regression validation was still in progress
when this checkpoint was made.

No claim of general HTML support, Direct2D parity, production acceptance,
clean sanitizers, or reliable performance improvement is made. The source
contains the existing engine; this checkpoint does not replace its layout
or HTML/CSS implementation.

Focused helper tests are in `native/tests/fractional_image_checkpoint`.
For example, from repository root (requires a C++20 compiler):

```sh
c++ -std=c++20 -O1 -g -UNDEBUG \
  -I native/src/rendering/software \
  native/tests/fractional_image_checkpoint/unit.cpp -o /tmp/aether-image-unit
/tmp/aether-image-unit
```

The independent rational fixtures are frozen. Do not regenerate them merely
to make a candidate pass. Generated binaries, logs, private validation trees,
and local workspace metadata are not included. Some broader historical tests
need external fixtures and the existing build documentation; this is not a
self-contained distribution of all test evidence.

No workflows were added or changed. GitHub Actions remain disabled per the
repository owner's explicit instruction. No workflow was dispatched.
