# Lithium Firmware Builds

Generated release files are local build output and are ignored by Git; this
guide remains tracked. Published downloads belong to the isolated R2 lithium
channel, with engineering artifacts also available from GitHub Actions.

`20261124-025551.0/CC2_LITHIUM.hex` is the current normal image with EXTERNAL/FORCE charging
enabled by default. Its charge-enable output can become active after plugging
in with ignition off. A proper lithium charger and independent BMS must
control the battery. These are bench firmware builds, not vehicle-qualified
releases.

`20261124-025551.0/CC2_LITHIUM_SAFE.hex` leaves charge, drive, fan and buzzer loads off.
Both images preserve/read original odometer and speed-calibration records.
Back up both EEPROMs before migration. No erase or blanking is required.

The matching protocol.json, identity build/digest, compiled transcripts,
desktop schema verification, source hashes and build metadata accompany the
images. New release versions use `YYYYMMDD-HHmmss.<run>` in Europe/Oslo, matching
the newer firmware and PC software. ID, IDENTIFY, `$ID#`, release metadata and
R2 downloads all use that same version. Local builds use run 0; there is no new
semantic version counter. The documented @B/@D/@E report layouts are unchanged.
Unknown SOC is omitted, never 255 percent.
RX overflow tails cannot execute commands or clear their own fault; blocked
service commands return interlock errors. PC live-voltage/current/mileage
decoding is included in the cross-repository regression checks.
The v0.2.0, v0.2.1 and v0.2.2 files are retained unchanged; v0.1.0 uses the retired custom format.
Run `tools/make_release.ps1` to rebuild, test and regenerate this release.

The GitHub workflow uploads build artifacts and optionally publishes only the
lithium R2 channel, not GitHub Releases. SAFE stays in build artifacts; the R2
normal download uses the dated build ID and matching immutable protocol.json.
