#!/usr/bin/env python3
"""Generate the OwlCarOS startup/theme chime (22050 Hz mono WAV)."""
import math
import struct
import wave

RATE = 22050
DURATION = 3.0
N = int(RATE * DURATION)

samples = [0.0] * N


def note(freq, start, length, gain):
    first = int(start * RATE)
    count = int(length * RATE)
    for i in range(count):
        idx = first + i
        if idx >= N:
            break
        t = i / RATE
        # fast attack, exponential decay
        env = min(1.0, t / 0.012) * math.exp(-t * 2.6)
        value = (
            math.sin(2 * math.pi * freq * t)
            + 0.28 * math.sin(2 * math.pi * freq * 2 * t)
            + 0.10 * math.sin(2 * math.pi * freq * 3 * t)
        )
        samples[idx] += gain * env * value


note(880.00, 0.00, 1.60, 0.32)   # A5
note(1318.51, 0.85, 2.10, 0.26)  # E6
note(1760.00, 1.70, 1.25, 0.12)  # A6 sparkle

peak = max(abs(s) for s in samples) or 1.0
scale = 0.85 / peak

with wave.open("owlcaros-theme.wav", "wb") as wav:
    wav.setnchannels(1)
    wav.setsampwidth(2)
    wav.setframerate(RATE)
    frames = bytearray()
    for s in samples:
        v = int(max(-1.0, min(1.0, s * scale)) * 32767)
        frames += struct.pack("<h", v)
    wav.writeframes(bytes(frames))

print("wrote owlcaros-theme.wav")
