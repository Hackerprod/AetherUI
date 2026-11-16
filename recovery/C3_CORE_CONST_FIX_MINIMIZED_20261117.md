# Minimized core const correction

Archive c3-core-const-fix-minimized-20261117.zip; SHA256 ac9a0e84b8b5849bd4f13d6a453a410c14afece0d1324b2141c3515e2c8b9e4f.
Five entries: corrected core source, relative patch, source hashes, recovery guide and manifest. Independently reviewed for disclosure; exact patch restoration and integrity verified. No private records, machine paths, logs or binaries.

Requires the matching prior C3 baseline with test copy-initialization correction. Changes only the contribution declaration from constexpr to const, preserving expression and uses. Source recovery is not a build/test pass. No main/workflow changes.
