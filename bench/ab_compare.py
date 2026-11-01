#!/usr/bin/env python3
"""Compare two docset_ab_* binaries (old = test/docset_old.hh, new = docset.hh) row by row.

Runs old, new, old again in every round (the second old run only feeds the A/A noise
column) and reports, per row, the best time of each binary over the rounds and old/new
(above 1 means new is faster). Both sides of old/new have one run per round, so neither is
favored by taking the better of two.
Usage: ab_compare.py OLD NEW [rounds=5] [universe_bits=22]
"""
import subprocess
import sys


def run(binary, bits):
    out = subprocess.run([binary, bits], check=True, capture_output=True, text=True).stdout
    rows = {}
    for line in out.splitlines():
        name, _, ns = line.rpartition(" ")
        rows[name.strip()] = float(ns)
    return rows


def main():
    old, new = sys.argv[1], sys.argv[2]
    rounds = int(sys.argv[3]) if len(sys.argv) > 3 else 5
    bits = sys.argv[4] if len(sys.argv) > 4 else "22"
    runs = {"a": [], "n": [], "b": []}
    for _ in range(rounds):
        runs["a"].append(run(old, bits))
        runs["n"].append(run(new, bits))
        runs["b"].append(run(old, bits))
    print(f"{'row':44s} {'old ns':>11s} {'new ns':>11s} {'old/new':>8s} {'range':>13s} {'A/A':>6s}")
    for name in runs["a"][0]:
        best = {k: min(r[name] for r in v) for k, v in runs.items()}
        per_round = [a[name] / n[name] for a, n in zip(runs["a"], runs["n"])]
        print(f"{name:44s} {best['a']:11.1f} {best['n']:11.1f} {best['a'] / best['n']:8.2f} "
              f"{min(per_round):6.2f}-{max(per_round):<6.2f} {best['a'] / best['b']:6.2f}")


main()
