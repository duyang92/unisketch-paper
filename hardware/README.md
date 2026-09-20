# P4 Hardware Implementation

`UniSketch.p4` is the P4_16 data-plane implementation of UniSketch. The
documented hardware target is a first-generation Intel Tofino switch, and the
program was developed with Intel P4 SDE 9.2.0.

## Requirements

- A first-generation Intel Tofino switch.
- Intel P4 SDE 9.2.0 installed under a valid vendor license.
- A host and switch configuration supported by that SDE installation.

The SDE, switch, traffic generator, and lab topology are not distributed with
this artifact. The software artifact does not emulate Tofino.

## Architecture Selection

The source selects the architecture include at compile time:

```p4
#if __TARGET_TOFINO__ == 2
#include<t2na.p4>
#else
#include<tna.p4>
#endif
```

For the documented first-generation target, the compiler selects `tna.p4`.
The conditional Tofino 2 include is retained in the source, but Tofino 2 is not
the evaluated hardware target.

## Compile

Configure `SDE` according to the vendor documentation. From the repository
root, verify the variable and compile the program:

```bash
test -n "${SDE:-}"
"${SDE}/p4_build.sh" hardware/UniSketch.p4
```

The program name produced by the standard SDE build is `UniSketch`.

## Inspect Resource Use

If P4 Insight is installed in the SDE environment, start it with:

```bash
"${SDE}/p4i.sh"
```

Open the address printed by the command and select the compiled UniSketch
program to inspect stage and memory allocation.

## Launch on the Switch

With the switch connected and the platform configured for the SDE, launch the
switch daemon:

```bash
"${SDE}/run_switchd.sh" -p UniSketch
```

Traffic injection, port configuration, and topology setup are specific to the
evaluator's switch environment and are not automated by this repository. The
software implementation under `simulation/` provides the estimator and a
hardware-independent Functional evaluation path.

## Verification Boundary

The P4 source and these commands were checked for consistency during artifact
packaging. The current software test host does not contain the proprietary SDE
or a Tofino switch, so the P4 build and hardware launch were not re-executed on
that host. No local hardware-execution claim is made by the software test log.
