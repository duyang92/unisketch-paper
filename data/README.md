# Bundled Example Input

`00.txt` is the example input bundled with the artifact for Functional
evaluation. It lets evaluators build and exercise UniSketch and the included
software baselines without preparing another trace.

## Format

Each non-empty line contains two whitespace-separated unsigned 32-bit decimal
integers:

```text
element_id flow_id
```

The program treats all `element_id` values associated with the same `flow_id`
as that flow's spread. Blank lines are ignored. A malformed row, an extra
column, a negative value, or a value larger than `4294967295` causes the
program to stop with an error that identifies the line number.

## File Information

- File size: 18,652,375 bytes
- Records: 907463
- SHA-256: `fe98bfa312a4db6a5c919c94e9428ec6e6f097f75d772f735ec079c3585f904b`

Verify the bundled file from the repository root:

```bash
wc -l data/00.txt
sha256sum data/00.txt
```

## Custom Input

Use another input file in the same format with the `--input` option:

```bash
./build/unisketch --algorithm unisketch --memory-kb 2048 \
  --input /path/to/input.txt --seed 1
```

`data/00.txt` is not covered by the repository's MIT License. No license grant
for this data file is made by this repository.
