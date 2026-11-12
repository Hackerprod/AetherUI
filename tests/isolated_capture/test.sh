#!/bin/sh
# Manual Linux tests. Each capture starts a new worker process.
set -eu
bin=${1:?binary}; out=${2:?NEW_OUTPUT_DIRECTORY}
mkdir "$out"
printf '%s' '<html><head><style>body{margin:0}div{display:block;width:40px;height:30px;background:red}</style></head><body><div></div></body></html>' > "$out/shape.html"
"$bin" --geometry "$out/shape.html" "$out/shape" 64 64 1
test -s "$out/shape/aether.png"
grep -q 'supervisor_status=GEOMETRY_RENDERED' "$out/shape/report.log"
grep -q 'css_eligibility=Unknown' "$out/shape/report.log"
grep -q 'flexbox_semantic_acceptance=false' "$out/shape/report.log"
reject() {
 name=$1; reason=$2
 if "$bin" --geometry "$out/$name.html" "$out/$name" 64 64 1; then echo "unexpected success $name"; exit 1; fi
 test ! -e "$out/$name/aether.png"
 grep -q "$reason" "$out/$name/report.log"
}
printf '%s' '<body>Hello</body>' > "$out/text.html"; reject text TEXT_RENDERER_UNAVAILABLE
printf '%s' '<body><img src="https://example.invalid/x.png"></body>' > "$out/resource.html"; reject resource RESOURCE_OR_SCRIPT_ELEMENT_UNSUPPORTED
printf '%s' '<style>@import "x.css";</style><body></body>' > "$out/import.html"; reject import CSS_AT_RULE_UNSUPPORTED
printf '%s' '<body style="background:url(x)"></body>' > "$out/url.html"; reject url CSS_RESOURCE_UNSUPPORTED
printf 'a\000b' > "$out/nul.html"; reject nul INPUT_NUL
# byte budget is enforced while reading, not from a racy stat-size check.
dd if=/dev/zero bs=1048576 count=3 of="$out/large.html" 2>/dev/null; reject large INPUT_BYTE_LIMIT
if "$bin" --css "$out/shape.html" "$out/css-domain" 64 64 1; then exit 1; fi
grep -q REQUESTED_CSS_DOMAIN_UNAVAILABLE "$out/css-domain/report.log"
test ! -e "$out/css-domain/aether.png"
# Existing destination cannot be overwritten, including previous accepted frame.
sha256sum "$out/shape/aether.png" > "$out/before-image.sha"
if "$bin" --geometry "$out/text.html" "$out/shape" 64 64 1; then exit 1; fi
sha256sum -c "$out/before-image.sha"
# Original page is read only. It is not simplified to make it pass.
fixture=tests/visual_compare/fixtures/flexbox_runtime_validation.html
sha256sum "$fixture" > "$out/original.sha"
if "$bin" --geometry "$fixture" "$out/flexbox-original" 1280 800 1; then echo 'unexpected full fixture success'; exit 1; fi
sha256sum -c "$out/original.sha"
grep -q TEXT_RENDERER_UNAVAILABLE "$out/flexbox-original/report.log"
test ! -e "$out/flexbox-original/aether.png"
echo 'PASS input/preflight/domain/geometry/artifact/original-fixture checks'
