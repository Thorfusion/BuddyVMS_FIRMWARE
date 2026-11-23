# Lithium Control Behavior

## Mode Selection

Modes are independent for charging and driving: 0 OFF, 1 EXTERNAL, 2 SUPERVISED.
Both default to EXTERNAL. FORCE is an alias for EXTERNAL, not a bypass of
ignition, movement, relay-conflict or controller-stall interlocks.

EXTERNAL means an independent BMS/charger controls battery safety electrically.
Serial BMS permissions and battery limits do not inhibit that mode. The CC
does not know when the BMS has stopped current, so CHG means a relay request,
not proof that current is flowing. External charge enable can remain requested
after the BMS stops charging, exactly as requested for this configuration.

## Charging Decisions

```text
Any latched CC fault?                       -> OFF, FAULT
Charger absent?                            -> OFF, IDLE; forget completion
Mode OFF?                                  -> OFF, WAIT
Ignition on?                               -> OFF, WAIT
Motion pulses/speed or drive relays active? -> OFF, WAIT
SUPERVISED with invalid limits?             -> OFF, WAIT
SUPERVISED with missing/stale adapter?       -> OFF; fault if previously charging
SUPERVISED measurements outside limits?     -> OFF, latched fault
SUPERVISED BMS says complete?               -> OFF, COMPLETE until unplugged
SUPERVISED without charge permission?       -> OFF, WAIT
All prerequisites continuously true 1 s?   -> charge enable requested
```

In SUPERVISED, losing permission restarts qualification, not a maintenance
cycle. A configured maximum continuous-charge time is enforced. Completion
does not overwrite reported SOC. There is no current-taper termination rule
inside the CC; the BMS/charger reports completion. In EXTERNAL, completion and
the maximum-charge timer are not enforced by the CC.

Raw charger removal or ignition-on prevents the physical charge-enable output
even before the debounced state catches up. FULL/balance is never requested.
There is no relay feedback or independently measured current-decay proof in
this first version; physical welded-contact detection is not implemented.

## Driving Decisions

External mode permits basic driving with ignition on and no charger present.
Supervised mode also requires valid limits, fresh adapter data, drive permission
and measurements within the drive limits. New drive engagement waits for speed
at most 1 km/h and no pending speed pulses, applies direction, waits 500 ms,
requests the motor relay, then waits 100 ms before the footswitch relay.

A changed direction drops motor and footswitch first, waits until speed is at
most 1 km/h, then waits 300 ms before changing direction and restarting the
sequence. Permission loss, charger insertion or ignition-off drops motor and
footswitch. Direction is retained rather than switched merely because drive
permission disappeared. A failed/disconnected speed sensor can look stopped;
there is no contactor or direction-feedback input qualified in this version.

## Supervised Limits

Configure exact pack limits; there is intentionally no preselected lithium
chemistry. The command is:

```text
$CONFIG,LIMITS,cells,min_mV,max_mV,charge_min_dC,charge_max_dC,drive_min_dC,drive_max_dC,max_charge_dA,max_discharge_dA,max_charge_seconds#
```

Current is positive into the battery. dA means 0.1 A; dC means 0.1 C. Cell
voltage and temperature limits are inclusive: equality is allowed; below a
minimum or above a maximum faults the applicable supervised operation. Both
current-direction limits are enforced while supervising. Cell count must
match configuration. Charging-temperature limits do not apply to unplugged
driving, and drive-temperature limits do not replace the charging limits.

The parser's broad physical bounds are only data validation, not recommended
operating limits. Use the installed cells/BMS/charger specifications.

## Faults And Reasons

Fault bits are latched and combined:

| Bit value | Meaning |
|---:|---|
| 1 | Adapter expired during supervised charge or enabled drive |
| 2 | BMS fault reported during the applicable supervised operation |
| 4 | Cell count or voltage outside configured limits |
| 8 | Temperature outside the applicable configured limits |
| 16 | Current outside configured limits |
| 32 | Supervised continuous-charge timeout |
| 64 | Foreground stopped servicing the 250 ms output lease |
| 128 | UART receive queue overflow |

Any latched fault disables charge, motor and footswitch requests. It does not
erase configuration or set SOC full. The normal image requests the buzzer.
FAULT,CLEAR requires ignition off, stopped motion, inactive drive relays and no
charge request. If either mode is supervised it also requires fresh, valid
measurements with no active applicable measurement/BMS fault. A renewed packet
alone does not clear a fault. Reset clears RAM faults; the stored mode returns
at boot and charging must qualify again. This is not a persistent crash lockout.

Reason values: 0 none, 1 mode disabled, 2 ignition on, 3 motion/drive outputs,
4 missing configuration, 5 adapter unavailable, 6 permission/inhibit,
7 completed, 8 latched fault. Charge states: 0 IDLE, 1 WAIT, 2 QUALIFY,
3 ON, 4 COMPLETE, 5 FAULT.

## Timing, Display And Cooling

The 1 ms timer tracks uptime. At 250 ms without foreground lease renewal, its
interrupt independently drops charge/motor/footswitch, keeps direction, and
latches expiry. A hardware watchdog is fed only after foreground control
progress. Actual watchdog timing/reset still needs validation on XC878.

Display refresh is 250 ms: speed and odometer remain available, the trip digits
show CHG for requested charging, FULL for supervised completion, or Err for a
CC fault. Battery bars use fresh adapter SOC only; stale/unknown SOC is blank.
The ECO bar remains blank. P3.1 is a controller-work LED, not a UART pin.

Fresh adapter temperature starts the cooling fan at or above 40.0 C and stops
it at or below 35.0 C by default. CONFIG,FAN,on_dC,off_dC changes this hysteresis.
Without fresh data there is no automatic cooling request. This optional
cooling convenience is not battery thermal protection. The independent BMS
must handle that protection even in FORCE mode. DC/DC-force is not used.

The SAFE image keeps charge, drive, fan and buzzer loads inactive regardless
of policy requests. It is useful for serial/display/storage bring-up.
