# Diagnostic Notes

## Goal

Understand why a `Heltec V4` node could hear activity in the city mesh but was
not receiving reliable ACKs or traceroute responses from neighboring nodes.

## What was checked

### Meshtastic-side checks

- switched the node to `RU / MEDIUM_FAST`
- checked visible neighbors
- tested direct `ACK`
- tested `traceroute`
- temporarily enabled and then disabled `Range Test`
- checked whether a control broadcast appeared through ONEmesh

### Raw-radio checks

- CAD on likely `RU / MEDIUM_FAST` slots
- RX windows on multiple frequencies
- FEM RX/TX pin switching
- packet TX burst
- power sweep from `2` to `20 dBm`
- boosted RX
- sync-word sweep
- continuous-carrier support added for external RF inspection

## Main observations

- The board clearly sees LoRa activity on `RU` frequencies.
- Meshtastic on the node sees many neighboring nodes, including direct ones.
- TX starts successfully at the SX1262 level.
- ACK requests still failed with `MAX_RETRANSMIT`.
- `traceroute` still stopped on the local node.
- Changing hop limit did not fix the problem.
- Boosted RX and sync-word sweep did not suddenly unlock packet decoding in the
  raw sketch.

## Working hypothesis

The evidence points away from a simple region/profile mistake and more toward a
one-way link or RF-path issue:

- antenna / pigtail / U.FL connection
- front-end module switching or PA path
- weak or degraded transmit chain

## Best next checks

If troubleshooting continues, the most useful external test is:

1. Run continuous carrier (`w`) or power sweep (`p`) from the raw sketch.
2. Observe the signal on a nearby SDR, tinySA, or second LoRa device.
3. Verify that RF output is present and rises as TX power increases.

That test gives a much stronger answer than continuing to tweak protocol-level
settings blindly.
