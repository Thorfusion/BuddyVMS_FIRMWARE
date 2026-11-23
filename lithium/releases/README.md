# Lithium Firmware Builds

`v0.1.0/CC2_LITHIUM.hex` is the normal image with EXTERNAL/FORCE charging
enabled by default. Its charge-enable output can become active after plugging
in with ignition off. A proper lithium charger and independent BMS must
control the battery. These are bench firmware builds, not vehicle-qualified
releases.

`v0.1.0/CC2_LITHIUM_SAFE.hex` leaves charge, drive, fan and buzzer loads off.
Both images preserve/read original odometer and speed-calibration records.
Back up both EEPROMs before migration. No erase or blanking is required.

Build metadata, source hashes and verification reports accompany the images.
Run `tools/make_release.ps1` to rebuild, test and regenerate this release.
