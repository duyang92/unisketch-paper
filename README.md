# UniSketch: A Unified Sketch for Efficient General Flow Spread Measurement

This repository contains the artifact for the paper **"UniSketch: A Unified
Sketch for Efficient General Flow Spread Measurement,"** submitted to ACM ATC
2026.

## Artifact Scope

This artifact targets the **Artifacts Available** and **Artifacts Functional**
badges. The primary evaluation path builds the software implementation and runs
a minimal working example of UniSketch. The repository also includes runnable
software baselines and the P4 data-plane implementation.

Reproducing every experiment, figure, and table in the paper is outside this
artifact's scope. In particular, the proprietary switch environment is not
bundled. The artifact is organized for source inspection, extension, and
functional testing with the supplied example input.

## Artifact Components

- `algorithms/unisketch/`: the core UniSketch software implementation.
- `algorithms/vBitmap/`: the vBitmap spread-estimation baseline.
- `algorithms/spreadSketch/`: the SpreadSketch super-spreader baseline, used
  with vBitmap in the `vbitmap-ss` mode.
- `algorithms/rskt/`: the RSKT baseline, used in the `vbitmap-ss-rskt` mode.
- `algorithms/m2d/`: the M2D baseline.
- `algorithms/kpse/`: support for K-persistent spread estimation.
- `simulation/main.cpp`: the validated command-line benchmark driver.
- `data/00.txt`: the bundled example input.
- `hardware/UniSketch.p4`: the P4_16 data-plane implementation.
- `utils/`: shared data structures and the public-domain MurmurHash3 code.
- `scripts/`: minimal, smoke-test, and all-algorithm workflows.
- `tests/`: CLI, sanitizer, workflow, and packaging checks.

## Requirements

The software path requires:

- Linux on an x86-64 processor with AVX2 support;
- G++ with C++17 support (G++ 9.4.0 or newer is recommended);
- GNU Make;
- Bash and standard GNU command-line tools.

Check AVX2 support before compiling:

```bash
grep -m1 -o avx2 /proc/cpuinfo
```

The command should print `avx2`. The sanitizer regression additionally uses the
AddressSanitizer and UndefinedBehaviorSanitizer runtimes supplied by GCC.

The P4 path has separate proprietary requirements described in
[`hardware/README.md`](hardware/README.md).

## Tested Environment

The software workflows were tested with:

- Ubuntu 20.04.5 LTS, Linux 5.15;
- Intel Xeon W-2295 at 3.00 GHz, 18 cores and 36 hardware threads;
- 251 GiB RAM;
- G++ 9.4.0;
- GNU Make 4.2.1.

The benchmark is single-threaded. It does not require the full memory capacity
of the tested host.

## Quick Start

From the repository root:

```bash
make
make run-minimal
```

The first command creates `build/unisketch`. The second runs UniSketch with a
2,048 KiB memory budget, seed `1`, SSD threshold `100`, and the bundled input.

For a quick check of UniSketch and every included baseline, using SSD threshold
`3` so that the first 1,000 records include positive examples:

```bash
make smoke-test
```

## Expected Output

Each selected algorithm prints a result block with the following fields:

```text
Algorithm: unisketch
Input records: 907463
Distinct flows: 161473
Memory: 2048 KiB
Insert throughput: <machine-dependent value> Mpps
Per-flow query time: <machine-dependent value> ns
Estimate checksum: <algorithm-dependent value>
PFSE MRE: <algorithm-dependent value>
SSD threshold: 100
Actual super-spreaders: 1366
Reported super-spreaders: <algorithm-dependent value>
SSD true positives: <algorithm-dependent value>
SSD false positives: <algorithm-dependent value>
SSD false negatives: <algorithm-dependent value>
SSD precision: <algorithm-dependent value>
SSD recall: <algorithm-dependent value>
SSD F1-score: <algorithm-dependent value>
```

Throughput and query time depend on the processor, compiler, system load, and
power-management settings. For the bundled input and threshold `100`, the input
record count, distinct-flow count, threshold, and actual-super-spreader count
shown above are exact reference values. A different value for any of these four
fields indicates that the input or ground-truth configuration differs.

`Estimate checksum` makes the PFSE query phase visible and prevents a benchmark
run from reporting only timing information. PFSE MRE is finite and
non-negative; SSD precision, recall, and F1-score are in `[0, 1]`. The exact
algorithm-dependent reference values for the documented Ubuntu/G++ environment
are listed in [Reference Results](#reference-results).

## Evaluated Tasks and Metrics

The bundled data contains one measurement period, so the executable evaluation
covers only these two tasks:

- **Per-flow spread estimation (PFSE).** For each flow, ground truth is the
  number of distinct elements in the input. The reported mean relative error is
  `mean(abs(actual - estimate) / actual)` over all input flows.
- **Super-spreader detection (SSD).** A flow is an actual super-spreader when
  its ground-truth spread is greater than or equal to `--ssd-threshold`. A flow
  is reported when its candidate estimate meets the same threshold. The driver
  reports precision, recall, and their harmonic mean (F1-score), together with
  the underlying actual, reported, true-positive, false-positive, and
  false-negative counts.

Repeated copies of the same `(flow_id, element_id)` pair count once in the
ground truth. If either the actual or reported SSD set is empty, its
corresponding precision or recall is reported as zero; F1 is zero when
precision plus recall is zero.

The algorithm modes use the following components for these tasks:

| Mode | PFSE estimate | SSD candidates |
| --- | --- | --- |
| `unisketch` | UniSketch per-flow query | UniSketch heavy part |
| `vbitmap-ss` | vBitmap | SpreadSketch |
| `vbitmap-ss-rskt` | vBitmap | SpreadSketch |
| `m2d` | M2D per-flow query | M2D heaps |

RSKT remains active in the insertion path of `vbitmap-ss-rskt`, but it is not
used for PFSE or SSD. KPSE and HSCD require multiple measurement periods and
are therefore outside the bundled single-period example.

## Reference Results

The following configuration is used by `make run-minimal` and `make run-all`:

```text
Memory: 2048 KiB
Seed: 1
SSD threshold: 100
Input: data/00.txt
```

For `make run-minimal` and `make run-all`, these ground-truth values must match
exactly:

| Field | Expected value |
| --- | ---: |
| Input records | 907463 |
| Distinct flows | 161473 |
| Actual super-spreaders | 1366 |

Reference results from a clean x86-64 run with G++ 13.3.0 (the counts should
match exactly; allow small floating-point differences across compilers):

| Algorithm | Estimate checksum | PFSE MRE | Reported / TP / FP / FN | SSD precision | SSD recall | SSD F1-score |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `unisketch` | 913308.955760 | 0.183453 | 1375 / 1324 / 51 / 42 | 0.962909 | 0.969253 | 0.966071 |
| `vbitmap-ss` | 2291631.709779 | 7.654162 | 1382 / 1196 / 186 / 170 | 0.865412 | 0.875549 | 0.870451 |
| `vbitmap-ss-rskt` | 2662286.075291 | 9.559185 | 1594 / 1060 / 534 / 306 | 0.664994 | 0.775988 | 0.716216 |
| `m2d` | 8739769.767945 | 40.941159 | 87 / 64 / 23 / 1302 | 0.735632 | 0.046852 | 0.088094 |

`make smoke-test` uses the first 1,000 records with the same memory and seed,
but SSD threshold `3`. It should report 796 distinct flows and 43 actual
super-spreaders. Its reference outputs are:

| Algorithm | Estimate checksum | PFSE MRE | Reported / TP | SSD F1-score |
| --- | ---: | ---: | ---: | ---: |
| `unisketch` | 1046.423106 | 0.067881 | 41 / 41 | 0.976190 |
| `vbitmap-ss` | 1225.990357 | 0.312395 | 39 / 39 | 0.951220 |
| `vbitmap-ss-rskt` | 1262.100542 | 0.362564 | 39 / 39 | 0.951220 |
| `m2d` | 828.254269 | 0.076036 | 4 / 4 | 0.170213 |

## Evaluation Workflows

The repository provides four main workflows:

```bash
make all          # Build build/unisketch.
make run-minimal  # Run the primary UniSketch Functional example.
make smoke-test   # Run every algorithm on 1,000 records, SSD threshold 3.
make run-all      # Run every algorithm on the complete bundled input.
```

`make run-minimal` is the recommended kick-the-tires path. `make smoke-test`
verifies that all software modes can execute with a small temporary subset.
`make run-all` is optional for Functional review and takes substantially longer
because it evaluates every baseline on all 907,463 records.

`run-minimal` and `run-all` use SSD threshold `100`, while `smoke-test` uses
`3`. To evaluate another threshold, invoke the executable directly as shown
below.

Run the automated software regression suite with:

```bash
make test
```

Clean generated binaries with:

```bash
make clean
```

All scripts resolve the repository root from their own location, so they may
also be invoked while the current directory is elsewhere.

## Command-Line Interface

The canonical interface is:

```bash
./build/unisketch \
  --algorithm unisketch \
  --memory-kb 2048 \
  --input data/00.txt \
  --seed 1 \
  --ssd-threshold 100
```

Supported algorithm names are:

- `unisketch`
- `vbitmap-ss`
- `vbitmap-ss-rskt`
- `m2d`
- `all`

Show the built-in help with:

```bash
./build/unisketch --help
```

For compatibility with the repository's original interface, the following
uses the default algorithm, input path, and seed:

```bash
./build/unisketch 2048
```

Invalid algorithms, missing option values, invalid memory sizes, unavailable
input files, empty inputs, zero or invalid SSD thresholds, and malformed rows
produce an English diagnostic on standard error and a nonzero exit status.

## Input Data

Each non-empty input line has this format:

```text
element_id flow_id
```

Both fields are unsigned 32-bit decimal integers. The first field identifies a
distinct element; the second identifies the flow with which that element is
associated. See [`data/README.md`](data/README.md) for the bundled file's size,
record count, checksum, validation rules, and license boundary.

## Extending the Artifact

To evaluate a custom trace, convert each flow-element observation to the input
format above and pass its path explicitly:

```bash
./build/unisketch --algorithm all --memory-kb 4096 \
  --input /path/to/input.txt --seed 7 --ssd-threshold 100
```

The public headers under `algorithms/` contain the sketch classes used by the
driver. A new experiment can include those headers, link
`utils/MurmurHash3.cpp`, and follow the construction and insertion calls in
`simulation/main.cpp`. When comparing algorithms, use the same input, memory
budget, seed, compiler flags, and host.

## Hardware Implementation

The P4_16 implementation is in `hardware/UniSketch.p4` and targets a
first-generation Intel Tofino switch with Intel P4 SDE 9.2.0. It is provided
for inspection and use in an appropriately licensed switch environment. The
SDK, switch, traffic generator, and lab topology are not included.

See [`hardware/README.md`](hardware/README.md) for compilation and launch
commands. The software workflows do not require switch hardware.

## Resource Expectations

The bundled input occupies about 18.7 MB. The configured sketch budget is
given in KiB, but total process memory is higher because the driver loads the
input and maintains the distinct-flow set in memory.

With the executable already built, the following measurements were observed on
an Intel Xeon Platinum 8573C host with Ubuntu 24.04 and G++ 13.3.0:

- `make run-minimal`: approximately 1.3 seconds elapsed and 35,772 KiB peak
  resident memory.
- `make run-all`: approximately 28.5 seconds elapsed and 67,224 KiB peak
  resident memory.

These values are guidance, not performance guarantees. They vary with the host,
compiler, system load, and power-management settings. Measure a run on another
machine with:

```bash
/usr/bin/time -v make run-minimal
```


## License

Unless otherwise noted, the project source code and documentation are licensed
under the MIT License in [`LICENSE`](LICENSE). `data/00.txt` is excluded from
that license. MurmurHash3 remains in the public domain under the notice in its
source files.
