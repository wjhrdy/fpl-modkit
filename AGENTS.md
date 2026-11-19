# fp L ModKit development

Target SIGMA fp L firmware 3.02 only. Read `README.md` and the affected
product's evidence before changing a firmware binding. Keep official firmware
and card images outside the repository. Build offline into a new directory;
check source changes with the host and Thumb tests. For Screen Flip use the
documented payload digest to verify a no-change rebuild. When a build differs,
report the new hash and distinguish offline, emulated and camera results.

Camera operations, commits, pushes and publication are separate from building.
Retain GPL attribution and `NOTICE` when moving code to another product.
