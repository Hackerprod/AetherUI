# T1.1 transport-only validation

2026-11-13, dependency-free local C++ build, assertions enabled.
Base local H21 3627acc, content equivalent to published main 0a29b89.

- Clean build: 71 core + 9 extra translation units and normal/probe CLI.
- T1 transport: 37 checks, fixture resources (not real fonts).
- H21 regression: 28802 checks, 1200 coordinate scenes, OOM=96.
- H20 regression: 4857 checks, OOM=45.
- git diff --check: PASS.

T1 checks cover clone strong references/baseline, list strong retention after
external owners release, off-thread destruction, failed-build preserving previous
list, allocation failure injection, invalid cluster/baseline rejection and legacy
C list behavior. These are synthetic ownership tests, not raster correctness.
Both inline routes remain legacy-only, with null native run. The transport
boundary is preparatory and cannot produce native text from HTML yet.

Provider, real fonts, shaping, mask generation, HTML text PNG, full original
flexbox fixture and Chromium differential: NOT_RUN. No new text rendering claim.
Actions and public ABI unchanged. No installation, downloaded headers or runtime
ABI declarations. Source hashes in transport are metadata, not authenticity checks.
