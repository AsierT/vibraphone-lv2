# Vibraphone LV2

Standalone LV2 vibraphone by TT Beats.

This is a duophonic vibraphone instrument. MIDI Note On triggers the played note plus one automatic harmony note calculated from Root, Scale, and Interval. It keeps only two notes active at a time.

## Features

- Root, Scale, Interval, Harmony Level, Harmony Direction, Scale Snap
- Tone, Mallet Hardness, Strike Noise, Bar Decay, Velocity Sens
- Amp Attack, Decay, Sustain, Release
- Tremolo Rate, Tremolo Depth, Width, Gain
- Spring Mix, Decay, Tone, Drive, Shake
- Delay Mix, Time, Feedback, Tape Tone, Wow Flutter, Tape Age, Head Mode
- MOD-style metadata with port groups
- S2400 insert-compatible build with audio inputs at ports 0/1 and outputs at ports 2/3

## Build

```bash
make
```

## ARM64

```bash
make arm64
```

## S2400 ARM64 Bundle

```bash
make clean
make arm64-s2400
```

The unpacked bundle is generated here:

```text
s2400-lv2/vibraphone.lv2
```

Copy it to Unraid:

```bash
scp -r s2400-lv2/vibraphone.lv2 root@10.10.20.61:/mnt/user/domains/ubuntu-arm/
```

Check ABI and exported descriptor:

```bash
make check-s2400
```

## Metadata

Plugin URI:

```text
https://github.com/AsierT/vibraphone-lv2#vibraphone
```

Brand/developer:

```text
TT Beats
```
