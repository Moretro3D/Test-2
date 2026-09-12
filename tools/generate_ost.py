#!/usr/bin/env python3
"""Genere trois OST originales PCM WAV 16 kHz mono pour TamaPoke."""
from pathlib import Path
import math, random, struct, wave

RATE = 16000
DURATION = 24.0
OUT = Path(__file__).resolve().parent / "sdcard" / "music"

THEMES = {
    "morning.wav": {
        "bpm": 92, "root": 261.63,
        "chords": [(0,4,7,11),(5,9,12,16),(7,11,14,17),(0,4,7,11)],
        "melody": [12,14,16,19,16,14,12,9, 7,9,12,14,12,9,7,4],
        "warm": 1.0, "drums": 0.45,
    },
    "lofi.wav": {
        "bpm": 76, "root": 220.00,
        "chords": [(0,3,7,10),(5,8,12,15),(3,7,10,14),(7,10,14,17)],
        "melody": [10,12,15,14,12,10,7,5, 7,10,12,10,7,5,3,0],
        "warm": 0.72, "drums": 0.72,
    },
    "night.wav": {
        "bpm": 62, "root": 196.00,
        "chords": [(0,3,7,10),(8,12,15,19),(5,8,12,15),(7,10,14,17)],
        "melody": [12,15,14,10,8,10,7,5, 3,7,10,8,7,5,3,0],
        "warm": 0.46, "drums": 0.22,
    },
}

def hz(root, semis):
    return root * (2.0 ** (semis / 12.0))

def osc(freq, t):
    # triangle douce + fondamentale sinusoidale
    phase = (t * freq) % 1.0
    tri = 4.0 * abs(phase - 0.5) - 1.0
    return 0.72 * math.sin(2 * math.pi * freq * t) + 0.28 * tri

def render(name, cfg):
    random.seed(name)
    beat = 60.0 / cfg["bpm"]
    total = int(DURATION * RATE)
    samples = []
    lp = 0.0
    for i in range(total):
        t = i / RATE
        beat_pos = t / beat
        bar = int(beat_pos // 4)
        chord = cfg["chords"][bar % len(cfg["chords"])]

        # accord feutre, arpège discret et basse
        pad = sum(osc(hz(cfg["root"], n - 12), t) for n in chord) / len(chord)
        arp_note = chord[int(beat_pos * 2) % len(chord)]
        arp = osc(hz(cfg["root"], arp_note), t) * 0.15
        bass = math.sin(2 * math.pi * hz(cfg["root"], chord[0] - 24) * t) * 0.28

        # melodie originale avec enveloppe courte
        step = int(beat_pos * 2)
        note = cfg["melody"][step % len(cfg["melody"])]
        local = (beat_pos * 2) % 1.0
        env = min(1.0, local * 10.0) * max(0.0, 1.0 - local * 0.82)
        melody = osc(hz(cfg["root"], note), t) * env * 0.24

        # kick tres doux, sans souffle aleatoire (evite le gresillement)
        within = beat_pos % 1.0
        kick_env = math.exp(-within * 13.0)
        kick = math.sin(2 * math.pi * (72 - within * 25) * t) * kick_env * 0.22
        raw = pad * 0.28 + arp + bass + melody + cfg["drums"] * kick
        # filtre passe-bas chaleureux
        alpha = 0.18 + cfg["warm"] * 0.10
        lp += alpha * (raw - lp)
        fade = min(1.0, t / 0.7, (DURATION - t) / 0.7)
        samples.append(int(max(-1.0, min(1.0, lp * fade * 0.72)) * 32767))

    OUT.mkdir(parents=True, exist_ok=True)
    with wave.open(str(OUT / name), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes(struct.pack("<%dh" % len(samples), *samples))
    print(f"{name}: {len(samples)/RATE:.1f}s")

for filename, theme in THEMES.items():
    render(filename, theme)
