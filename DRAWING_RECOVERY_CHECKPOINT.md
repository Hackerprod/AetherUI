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

## Additional experimental clips checkpoint

The later clip-aware candidate is now preserved in this branch. It reuses the
existing coverage sampler and feeds its integer count into bounded image
arithmetic; this is exact arithmetic conditional on that sampler, not a proof
of universal curve accuracy. Out-of-domain/platform fallback remains legacy.

Independent validation approved experimental contract v3 and a bounded
operational success/cancellation campaign. The corpus's earlier 233 discrepancy
routes are preserved as historical evidence, not 233 new renders. Regression
coverage is 27 C++ results reused from this same clip-aware renderer plus one
new contract-v3 test, and 28 HTML tests/124 buffers reused with verified custody.
Earlier v1/v2 compatibility failures remain failures: v3 explicitly accepts two
additional changed bytes after independent rational reconstruction of frame25.
Only frames9 and25 have that rational reconstruction claim; the other30 retain
compatibility references. Do not describe this as unconditional compatibility.

The two-process operational observation passed exact buffers, atomicity and
cancellation checks in its reviewed scene. It establishes neither universal
performance nor a cancellation SLA. Host isolation, real workload, performance
budget, Direct2D parity and production acceptance remain unverified; the prior
LSan blocker remains open.

Recovery fixtures and frozen helpers are under
`recovery/clips-checkpoint-20261116.zip` (extract into a new directory); old and new helper namespaces are needed
by those comparison tests. The versioned v3 test is additionally placed under
`native/tests` for its relative engine include. Prior test contracts were not
rewritten. Documentation of earlier checkpoint results above is historical,
not the latest clip-aware status. No workflows or main-branch state changed.
