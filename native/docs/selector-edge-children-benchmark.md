# First/only-child benchmark

`tests/benchmark_selector_edge_children.cpp` is a deterministic fixture generator
and a real-runtime timing harness. Build **the identical harness** twice: once
against exact baseline `37ac372`, once against the candidate. Use the same
compiler, flags, machine and environment; keep each binary paired with its own
`libaether_core`. This is not a timing comparison against a handwritten oracle.

## Build and run

For each checkout, copy the same harness into `native/tests/` if absent, then:

```sh
cmake -S native -B build-edge -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DAETHERUI_BUILD_TESTS=OFF
cmake --build build-edge --target aether_core -j2
c++ -std=c++20 -O3 -DNDEBUG -ffp-contract=off \
  -I native/include -I native/src/style_runtime \
  native/tests/benchmark_selector_edge_children.cpp \
  native/src/style_runtime/selector_parser.cpp \
  native/src/style_runtime/selector_matcher.cpp \
  -L "$PWD/build-edge" -Wl,-rpath,"$PWD/build-edge" -laether_core \
  -o build-edge/benchmark_selector_edge_children
build-edge/benchmark_selector_edge_children > edge.csv
```

Core symbols for the private matcher are hidden, so the executable compiles that
revision's real parser/matcher; public resolution calls that revision's shared
core. No production instrumentation is required. The candidate also provides the
opt-in CMake target `aether_selector_edge_children_benchmark`, marked
`EXCLUDE_FROM_ALL`. Configure with `BUILD_TESTING=ON` and
`AETHERUI_BUILD_TESTS=ON`, then build that target explicitly. It compiles those
three sources and links `AetherUI::Core`; the manual recipe above works for the
original baseline, where that target is absent. The benchmark is not an ordinary
CTest performance gate.

The optional sole argument changes measured sample count (1–1000; default 15).
Every fixture has two additional unreported warmups. Use `... 1` for a smoke run.
Capture compiler version, full baseline/candidate commits, harness hash and build
flags with both CSV files. Alternate baseline/candidate process order for repeated
runs; summarize per-fixture medians, ranges and doubling ratios rather than one
aggregate speedup. No absolute timing threshold is asserted.

## Fixtures and checks

Each size is 512, 1024, 2048 or 4096. All fixture construction, HTML parsing,
selector parsing and stylesheet compilation happen outside timers.

- `all-elements`: n element siblings, no text.
- `constant-text-prefix`: 8 distinct text siblings then n elements.
- `proportional-text-prefix`: n distinct text siblings then n elements.
- `interleaved-text`: n repetitions of one text node followed by one element.
- `singleton-text-margins`: **one** element with n distinct text nodes on each
  side. Here n is margin size, not number of elements; CSV reports both counts.

Skipped processing instructions separate text tokens during HTML construction.
The harness explicitly verifies the resulting element and text counts; the
fixture does not confuse string length with sibling count. Both phases use the
same parsed DOM and `.item:first-child` or `.item:only-child` selector. The former
must match the first element; the latter matches only the singleton fixture.

Each matcher sample scans all subjects four times and reports microseconds per
scan, with a checked match checksum. `resolve-cold` measures only the public
`aether_style_runtime_resolve_subtree` call. Runtime cache clearing happens before
the timer; returned update inspection and disposal happen after it. This measures
cold-style resolution on an already constructed DOM (the current API internally
traverses the complete document even when passed the fixture root), not layout, paint, a warmed
no-op resolve or total application latency. Every resolution verifies exact
update count, unique coverage of all subjects and surrounding control elements, selected opacity 0.8,
unselected opacity 0.2 and control opacity 1.0. All checks remain active under
`NDEBUG`; failures abort the run. No generated content is introduced here;
semantic boundary tests cover generated and malformed nodes separately.

## Deterministic work counts

Separate untimed `visits` rows count inspected entries of `parent.children`,
including skipped text, for models of the original and candidate algorithms.
They are **models, not instrumented production counters**, and do not include
selector dispatch, style runtime traversal, allocation or construction. Per-node
model results must agree with each other, the fixture's expected result, and the
real matcher. Timing rows have zero visit fields rather than mixing counted
execution into wall-clock measurements.

Let E be element count, P leading text count, S trailing text count and
T = E(E+1)/2. For prefix fixtures:

| Selector | Original visits per full scan | Candidate visits per full scan |
| --- | ---: | ---: |
| first-child | E·P + T | E·(P+1) |
| only-child (E > 1, S = 0) | E·P + T + P + E | E·(P+1) + 1 |
| first-child singleton | P+1 | P+1 |
| only-child singleton | 2P+S+2 | P+S+2 |

For interleaved text, original first-child costs E(E+1), candidate 2E;
only-child adds 2E to original and 1 to candidate.

Thus the candidate removes the quadratic element-position scan for ordinary
siblings and fixed text prefixes. **A proportional leading text prefix still
costs Θ(n²)** because it is rescanned for each element. Singleton margins remain
linear, and first-child cannot improve there. This optimization introduces no
cache and makes no claim of universal linear behavior. Public resolution also
includes unaffected work, so matcher speedup is not an end-to-end speedup claim.

## Recorded comparison (2026-11-10)

Linux x86_64, GCC 14.2.0; both manual executables used C++20, `-O3 -DNDEBUG -ffp-contract=off`. Core builds were Release. Baseline was exact commit `37ac37249c59136f0b5a6834e0c706e2be294985`; candidate was the working tree based on that commit with the endpoint-matcher change. Builds and test suites had finished before measurement. Processes ran sequentially in order baseline 1, candidate 1, baseline 2, candidate 2. No CPU affinity or frequency isolation was imposed; these are observed microbenchmarks, not statistical confidence bounds.

Each table cell is median [minimum, maximum] in microseconds over 30 measured samples (15 in each process). Each process additionally ran two unreported warmups per fixture. Matcher values are per full subject scan, not per node. `speedup` is baseline median / candidate median; values below 1 mean the candidate was slower.

Abbreviations: elements = all-elements, prefix-8 = constant-text-prefix, prefix-n = proportional-text-prefix, interleaved = interleaved-text, singleton = singleton-text-margins. Singleton n is text nodes **per margin**, with one subject.

### matcher

| Shape | n | Selector | Baseline µs | Candidate µs | Speedup |
| --- | ---: | --- | ---: | ---: | ---: |
| elements | 512 | first-child | 182.038 [173.113, 239.107] | 9.267 [8.508, 12.186] | 19.64× |
| elements | 512 | only-child | 188.863 [177.868, 304.801] | 10.415 [9.254, 29.119] | 18.13× |
| elements | 1024 | first-child | 750.624 [734.603, 1056.920] | 19.497 [17.932, 46.698] | 38.50× |
| elements | 1024 | only-child | 765.438 [723.691, 1428.407] | 19.024 [18.405, 29.499] | 40.24× |
| elements | 2048 | first-child | 3087.024 [2942.370, 4796.592] | 44.538 [38.273, 94.623] | 69.31× |
| elements | 2048 | only-child | 3044.263 [2928.492, 5475.239] | 45.892 [39.019, 556.697] | 66.34× |
| elements | 4096 | first-child | 12234.262 [11713.370, 13719.471] | 94.963 [86.392, 212.122] | 128.83× |
| elements | 4096 | only-child | 12339.006 [11855.824, 13727.800] | 93.310 [86.235, 154.745] | 132.24× |
| prefix-8 | 512 | first-child | 186.425 [182.149, 334.027] | 10.349 [10.115, 10.631] | 18.01× |
| prefix-8 | 512 | only-child | 190.106 [180.462, 705.932] | 10.630 [10.436, 66.269] | 17.88× |
| prefix-8 | 1024 | first-child | 752.351 [739.160, 1132.048] | 21.152 [20.804, 28.183] | 35.57× |
| prefix-8 | 1024 | only-child | 750.980 [739.706, 1013.725] | 22.233 [20.864, 53.889] | 33.78× |
| prefix-8 | 2048 | first-child | 3074.888 [2960.071, 4210.310] | 49.481 [44.892, 102.669] | 62.14× |
| prefix-8 | 2048 | only-child | 3086.282 [2928.273, 4420.874] | 49.074 [46.295, 70.899] | 62.89× |
| prefix-8 | 4096 | first-child | 12141.320 [11776.389, 13502.549] | 102.766 [97.103, 301.934] | 118.15× |
| prefix-8 | 4096 | only-child | 12173.796 [11612.050, 13400.264] | 104.137 [95.321, 142.327] | 116.90× |
| prefix-n | 512 | first-child | 445.122 [365.135, 606.217] | 168.236 [159.002, 272.335] | 2.65× |
| prefix-n | 512 | only-child | 392.274 [370.810, 480.055] | 182.602 [164.971, 243.877] | 2.15× |
| prefix-n | 1024 | first-child | 1516.510 [1440.029, 2492.530] | 775.042 [731.576, 1094.216] | 1.96× |
| prefix-n | 1024 | only-child | 1498.676 [1446.496, 2504.844] | 762.988 [726.914, 882.337] | 1.96× |
| prefix-n | 2048 | first-child | 5996.444 [5752.184, 12556.367] | 3003.520 [2816.668, 3179.116] | 2.00× |
| prefix-n | 2048 | only-child | 5832.850 [5682.224, 6300.840] | 3055.971 [2892.522, 3567.606] | 1.91× |
| prefix-n | 4096 | first-child | 25655.798 [24098.978, 27207.863] | 11828.131 [11146.985, 22357.768] | 2.17× |
| prefix-n | 4096 | only-child | 25888.717 [24369.550, 28657.206] | 11864.936 [11237.481, 14474.746] | 2.18× |
| interleaved | 512 | first-child | 284.727 [275.965, 420.881] | 9.663 [9.399, 22.429] | 29.46× |
| interleaved | 512 | only-child | 285.232 [276.418, 745.892] | 10.071 [9.872, 16.387] | 28.32× |
| interleaved | 1024 | first-child | 1179.541 [1105.335, 1925.535] | 23.291 [19.725, 80.689] | 50.64× |
| interleaved | 1024 | only-child | 1147.304 [1107.837, 1225.546] | 23.102 [20.433, 46.803] | 49.66× |
| interleaved | 2048 | first-child | 4669.376 [4426.170, 5354.271] | 62.713 [50.631, 86.873] | 74.46× |
| interleaved | 2048 | only-child | 4675.318 [4441.846, 5550.274] | 57.792 [49.762, 159.220] | 80.90× |
| interleaved | 4096 | first-child | 20219.183 [19014.319, 23793.873] | 136.199 [123.110, 353.700] | 148.45× |
| interleaved | 4096 | only-child | 19757.438 [19207.315, 23885.741] | 152.175 [130.932, 192.424] | 129.83× |
| singleton | 512 | first-child | 0.334 [0.328, 0.418] | 0.338 [0.326, 0.569] | 0.99× |
| singleton | 512 | only-child | 0.959 [0.947, 6.257] | 0.709 [0.699, 0.784] | 1.35× |
| singleton | 1024 | first-child | 0.716 [0.706, 0.774] | 0.716 [0.704, 0.791] | 1.00× |
| singleton | 1024 | only-child | 2.030 [2.018, 5.881] | 1.386 [1.372, 5.283] | 1.47× |
| singleton | 2048 | first-child | 1.415 [1.392, 2.136] | 1.418 [1.400, 1.935] | 1.00× |
| singleton | 2048 | only-child | 4.050 [4.016, 8.044] | 2.767 [2.727, 2.920] | 1.46× |
| singleton | 4096 | first-child | 2.889 [2.864, 2.959] | 2.902 [2.857, 510.676] | 1.00× |
| singleton | 4096 | only-child | 8.457 [8.328, 8.836] | 5.993 [5.881, 513.678] | 1.41× |

### resolve-cold

| Shape | n | Selector | Baseline µs | Candidate µs | Speedup |
| --- | ---: | --- | ---: | ---: | ---: |
| elements | 512 | first-child | 2791.881 [2489.586, 3450.872] | 2178.458 [2131.557, 2463.176] | 1.28× |
| elements | 512 | only-child | 2768.640 [2531.138, 3488.879] | 2196.325 [2150.966, 2975.397] | 1.26× |
| elements | 1024 | first-child | 7023.197 [6498.999, 9260.394] | 5234.000 [4813.102, 8457.085] | 1.34× |
| elements | 1024 | only-child | 7255.727 [6349.613, 12997.657] | 5090.339 [4676.766, 7175.977] | 1.43× |
| elements | 2048 | first-child | 23222.031 [18247.886, 40879.448] | 13359.411 [10835.804, 18959.697] | 1.74× |
| elements | 2048 | only-child | 22483.279 [18717.023, 35350.411] | 14677.354 [10824.828, 36601.501] | 1.53× |
| elements | 4096 | first-child | 66182.220 [58779.944, 75211.053] | 43414.447 [34482.623, 58159.248] | 1.52× |
| elements | 4096 | only-child | 69364.650 [62644.398, 80465.593] | 37948.473 [32267.771, 57221.081] | 1.83× |
| prefix-8 | 512 | first-child | 2627.184 [2415.755, 4084.395] | 2177.462 [2015.883, 2663.658] | 1.21× |
| prefix-8 | 512 | only-child | 2548.785 [2411.098, 6825.800] | 2151.057 [2003.695, 3304.843] | 1.18× |
| prefix-8 | 1024 | first-child | 6662.990 [6152.938, 7843.863] | 4705.270 [4441.022, 5109.137] | 1.42× |
| prefix-8 | 1024 | only-child | 7112.096 [6002.852, 8841.103] | 5204.546 [4422.474, 6524.947] | 1.37× |
| prefix-8 | 2048 | first-child | 21755.794 [19582.064, 29661.149] | 13145.093 [11558.361, 15297.126] | 1.66× |
| prefix-8 | 2048 | only-child | 22152.471 [19552.590, 26584.932] | 13339.646 [11850.471, 15451.309] | 1.66× |
| prefix-8 | 4096 | first-child | 65207.628 [59642.812, 73671.320] | 38644.970 [27853.669, 62093.186] | 1.69× |
| prefix-8 | 4096 | only-child | 65518.823 [60740.269, 79737.878] | 36195.781 [28743.818, 45751.539] | 1.81× |
| prefix-n | 512 | first-child | 4221.077 [2758.511, 10879.094] | 2691.290 [2382.685, 7086.974] | 1.57× |
| prefix-n | 512 | only-child | 3041.851 [2770.148, 5594.016] | 2704.510 [2340.311, 3261.157] | 1.12× |
| prefix-n | 1024 | first-child | 9283.403 [7516.949, 15897.879] | 6857.369 [6111.085, 11429.938] | 1.35× |
| prefix-n | 1024 | only-child | 8903.001 [7943.001, 17550.210] | 7051.165 [6483.545, 8080.178] | 1.26× |
| prefix-n | 2048 | first-child | 29305.362 [26289.543, 63568.763] | 21373.425 [18973.979, 26359.231] | 1.37× |
| prefix-n | 2048 | only-child | 29081.521 [25979.376, 33395.478] | 22698.001 [19869.766, 32275.052] | 1.28× |
| prefix-n | 4096 | first-child | 97124.260 [91142.382, 121798.307] | 73037.334 [60809.776, 123629.445] | 1.33× |
| prefix-n | 4096 | only-child | 99814.138 [88873.812, 132974.578] | 67558.812 [56582.466, 92283.858] | 1.48× |
| interleaved | 512 | first-child | 3090.931 [2649.366, 4518.108] | 2295.419 [2055.062, 2923.008] | 1.35× |
| interleaved | 512 | only-child | 2958.552 [2582.515, 6288.672] | 2077.425 [1988.682, 2581.805] | 1.42× |
| interleaved | 1024 | first-child | 8324.761 [7423.900, 14205.395] | 5283.570 [4502.956, 9187.233] | 1.58× |
| interleaved | 1024 | only-child | 7701.132 [7139.602, 10184.874] | 5088.051 [4614.113, 8590.405] | 1.51× |
| interleaved | 2048 | first-child | 26777.105 [23324.805, 35707.726] | 14062.504 [12100.817, 23809.895] | 1.90× |
| interleaved | 2048 | only-child | 27402.731 [23870.305, 33555.840] | 13599.557 [11649.118, 20082.506] | 2.01× |
| interleaved | 4096 | first-child | 88712.477 [74315.976, 139507.755] | 39803.660 [29618.275, 68320.377] | 2.23× |
| interleaved | 4096 | only-child | 84837.688 [76602.287, 95726.000] | 39731.861 [33086.143, 53907.776] | 2.14× |
| singleton | 512 | first-child | 14.201 [13.891, 16.084] | 14.346 [13.600, 27.882] | 0.99× |
| singleton | 512 | only-child | 15.048 [14.652, 43.535] | 14.707 [14.211, 16.915] | 1.02× |
| singleton | 1024 | first-child | 18.753 [18.027, 44.537] | 19.133 [18.367, 44.968] | 0.98× |
| singleton | 1024 | only-child | 21.412 [20.741, 23.595] | 20.626 [19.629, 45.238] | 1.04× |
| singleton | 2048 | first-child | 31.462 [30.647, 99.880] | 32.248 [30.867, 115.414] | 0.98× |
| singleton | 2048 | only-child | 37.416 [36.465, 170.246] | 35.068 [33.931, 2083.314] | 1.07× |
| singleton | 4096 | first-child | 55.739 [54.893, 121.573] | 55.924 [54.813, 74.342] | 1.00× |
| singleton | 4096 | only-child | 68.013 [65.709, 91.297] | 62.514 [60.952, 98.528] | 1.09× |

### Reproducibility and limits

All four runs completed successfully, each with 40 visit rows and 1,200 timed rows. The 40 deterministic visit rows were byte-for-byte equivalent as parsed CSV records across all runs. Every match checksum and computed-style check passed under `NDEBUG`. After all timing runs, the registered CMake target was built successfully and its one-sample smoke run passed all 40 scenarios with identical visit rows (`build-benchmark-target.log`, `benchmark-edge-cmake-smoke.csv`). A single aggregate speedup would hide the different complexity classes and is intentionally omitted. Public resolution includes allocations, copying, document traversal and other unchanged work, which can dominate the optimized selector. In particular, the singleton and proportional-prefix fixtures do not support a universal linear-time or universal end-to-end speedup claim.

SHA-256 source identities:

- Harness: `82f5f6560b00d3e9d62b1064215d2af31acbada66d452bdf9129dacc25793ffd`.
- Baseline matcher: `9aafe0927a827147d07dff69c75d9239e6e3aec097e04f3273f281fc1909c96a`.
- Candidate matcher: `494a84347287dcd481949eb6bd56bdb826c581222437f83840caad396cee4c7b`.

Raw CSV artifact names and SHA-256 (retained in the validation evidence directory, not committed):

- `benchmark-edge-baseline-run1.csv`: `d27dcf19dfa9289a6079356c378cb983bf54fffc7c1c28fbcd908d758916c264`.
- `benchmark-edge-baseline-run2.csv`: `b827c817b9cffa205290be09ef6bf57d33d361294e45085d2ecbf0aa3620fcd6`.
- `benchmark-edge-candidate-run1.csv`: `f73f46e522a440e725fd009c71071ee5583560a1a1291e67d0c9ce32f0b1c6fd`.
- `benchmark-edge-candidate-run2.csv`: `ae8568ce015b8217e3375d5d0c2f4d9b6a32eb7ae929c7c2afcbcb818a8139a6`.

## Semantic and integration validation

- Full native Release suite: 260/260 tests passed.
- The edge-children regression ran against the real baseline and candidate:
  27,779 checks each, with identical mask/style digest `d20a4d4b1acc225c`.
  Its explicit invariants remain active under `NDEBUG`.
- ASan and UBSan: 10/10 focused structural/generated-selector tests passed with
  `detect_leaks=0`. LeakSanitizer was **not validated**: its runtime aborted with
  an incompatibility with ptrace. These runs do not establish absence of leaks.
- Installed C11 package consumer passed; exported symbols matched the baseline.
- No Chromium execution or browser conformance result is included.
