import csv
import sys
from pathlib import Path

import matplotlib.pyplot as plt

def load_histogram(path):
    values, counts = [], []
    with open(path) as f:
        for row in csv.DictReader(f):
            values.append(int(row["value"]))
            counts.append(int(row["count"]))
    return values, counts


def main():
    for required in ("raw_histogram.csv", "residual_histogram.csv"):
        if not Path(required).exists():
            print(f"error: {required} not found. Run ./build/milestone_0 first.")
            sys.exit(1)

    raw_v, raw_c = load_histogram("raw_histogram.csv")
    res_v, res_c = load_histogram("residual_histogram.csv")

    fig, axes = plt.subplots(1, 2, figsize=(12, 4))

    axes[0].bar(raw_v, raw_c, width=1.0, color="steelblue")
    axes[0].set_title("Raw pixels")
    axes[0].set_xlabel("Pixel value (0-255)")
    axes[0].set_ylabel("Count")

    axes[1].bar(res_v, res_c, width=1.0, color="indianred")
    axes[1].set_title("Residuals after Paeth")
    axes[1].set_xlabel("Residual value")
    axes[1].set_ylabel("Count")
    axes[1].set_xlim(-128, 128)

    plt.tight_layout()
    plt.savefig("histograms.png", dpi=120)
    print("Wrote histograms.png")


if __name__ == "__main__":
    main()
