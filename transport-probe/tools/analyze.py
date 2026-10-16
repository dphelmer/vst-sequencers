#!/usr/bin/env python3
"""Summarise ~/Library/Logs/TransportProbe/blocks.csv written by the TransportProbe plug-in."""
import csv, math, os, sys

d = os.path.expanduser("~/Library/Logs/TransportProbe")
rows = list(csv.DictReader(open(os.path.join(d, "blocks.csv"))))
if not rows:
    sys.exit("blocks.csv is empty (process() never called?)")

# ProcessContext state bits / BlockFlags (see ivstprocesscontext.h, probelog.h)
PLAY, CYCLE, REC = 1 << 1, 1 << 2, 1 << 3
SYSTIME, PPQ, TEMPO, BAR, CYCLEV, TSIG = 1 << 8, 1 << 9, 1 << 10, 1 << 11, 1 << 12, 1 << 13
HASCTX, JUMP, START, STOP, GEN = 1, 2, 4, 8, 16

st = [int(r["state"]) for r in rows]
fl = [int(r["flags"]) for r in rows]
n = len(rows)
cnt = lambda pred: sum(1 for i in range(n) if pred(st[i], fl[i]))

print(f"blocks logged: {n}; block sizes seen: {sorted({int(r['numSamples']) for r in rows})}")
print(f"processMode values: {sorted({int(r['processMode']) for r in rows})}  (0 realtime, 1 prefetch, 2 offline)")
print(f"blocks with ProcessContext: {cnt(lambda s,f: f & HASCTX)}")
for name, bit in [("kPlaying", PLAY), ("kCycleActive", CYCLE), ("kRecording", REC), ("systemTime valid", SYSTIME),
                  ("projectTimeMusic valid", PPQ), ("tempo valid", TEMPO), ("barPosition valid", BAR),
                  ("cycle valid", CYCLEV), ("timeSig valid", TSIG)]:
    print(f"  {name:24s} set in {cnt(lambda s,f: s & bit):6d} blocks")
stopped = cnt(lambda s,f: not (s & PLAY))
print(f"process() calls while stopped: {stopped}  (UNTESTED-in-Live question: >0 means Live keeps calling)")
print(f"play starts: {cnt(lambda s,f: f & START)}, play stops: {cnt(lambda s,f: f & STOP)}, jumps: {cnt(lambda s,f: f & JUMP)}")
print(f"notes emitted: {sum(int(r['beatsEmitted']) for r in rows)}, offs: {sum(int(r['offsEmitted']) for r in rows)}, CCs: {sum(int(r['ccEmitted']) for r in rows)}")
virt = sum(int(r.get("virtSent", 0) or 0) for r in rows)
print(f"virtual-port MIDI packets sent: {virt}")
print(f"input events seen: {sum(int(r['numInEvents']) for r in rows)}")

print("\nJumps (ppq vs expected):")
shown = 0
for r, f in zip(rows, fl):
    if f & JUMP and shown < 15:
        print(f"  seq {r['seq']}: ppq={r['ppq']} expected={r['expectedPpq']} cycle={r['cycleStart']}..{r['cycleEnd']} cycleActive={bool(int(r['state']) & CYCLE)}")
        shown += 1

# samples-vs-ppq linearity (the "tempo change makes sample time non-linear" report)
bad = 0
for a, b in zip(rows, rows[1:]):
    if int(b["flags"]) & (JUMP | START) or not (int(a["state"]) & PLAY) or not (int(b["state"]) & PLAY):
        continue
    if int(b["projectTimeSamples"]) - int(a["projectTimeSamples"]) != int(a["numSamples"]):
        bad += 1
print(f"\nblocks where projectTimeSamples did not advance by numSamples (non-jump): {bad}")

evp = os.path.join(d, "events.csv")
if os.path.exists(evp):
    ev = list(csv.DictReader(open(evp)))
    print(f"input event rows: {len(ev)} (type 0=noteOn 1=noteOff); first 5: {ev[:5]}")
