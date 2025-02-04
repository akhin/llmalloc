import matplotlib.pyplot as plt
import numpy as np

# Data from the new table
allocators = [
    "GNU LibC",
    "IntelOneTBB 2022.0.0",
    "mimalloc 2.1.9 (2025 01 03)",
    "llmalloc 1.0.0",
]

thread_4 = [635589803, 545012211, 371499829, 283114796]
thread_8 = [743908160, 643875768, 470252501, 397058964]
thread_16 = [1021612655, 951095911, 772204102, 698785232]

# Setting up the bar width and positions
x = np.arange(len(allocators))
width = 0.25

# Plotting the data
fig, ax = plt.subplots(figsize=(10, 6))
bars1 = ax.bar(x - width, thread_4, width, label="Thread Count = 4")
bars2 = ax.bar(x, thread_8, width, label="Thread Count = 8")
bars3 = ax.bar(x + width, thread_16, width, label="Thread Count = 16")

# Adding labels, title, and legend
#ax.set_xlabel("Allocator", fontsize=12)
ax.set_ylabel("Clock Cycles", fontsize=12)
#ax.set_title("Clock Cycle Benchmark Comparison Across Thread Counts", fontsize=14)
ax.set_xticks(x)
ax.set_xticklabels(allocators, rotation=15, ha="right", fontsize=10)
ax.legend()

# Adding value labels to the bars
def add_labels(bars):
    for bar in bars:
        height = bar.get_height()
        ax.text(
            bar.get_x() + bar.get_width() / 2,
            height + 10**7,
            f"{height:.0f}",
            ha="center",
            va="bottom",
            fontsize=8,
        )

add_labels(bars1)
add_labels(bars2)
add_labels(bars3)

# Adjust layout and display the plot
plt.tight_layout()
plt.show()