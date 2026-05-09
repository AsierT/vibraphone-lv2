> **WIP:** Work in progress. DSP behavior, LV2 metadata, presets, and build targets may change.

# Vibraphone LV2

Standalone LV2 vibraphone by TT Lab.

This is a polyphonic vibraphone instrument. In interval modes, MIDI Note On triggers the played note plus one automatic harmony note calculated from Scale and Interval. Setting Interval to Polyphonic plays incoming MIDI notes normally with up to 16 voices.

## Features

- Scale, Interval, Interval Fine Detune, Harmony Level, Harmony Direction, Scale Snap
- Tone, Mallet Hardness, Strike Noise, Bar Decay, Velocity Sens
- Amp Attack, Hold, Decay, Sustain, Release
- Filter Type, Cutoff, Resonance, Drive, Env Amount, Attack, Hold, Decay, Sustain, Release
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
scp -r s2400-lv2/vibraphone.lv2 root@[IP]:'/mnt/user/Musica/Desarrollo LV2/'
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
TT Lab
```

## License

GPL-2.0-only.
