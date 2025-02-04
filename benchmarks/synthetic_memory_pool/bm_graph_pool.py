import matplotlib.pyplot as plt
import numpy as np

# Data from the final table
allocators = [
    "IntelOneTBB 2022.0.0 mempool",
    "llmalloc 1.0.0 mempool",
]
thread_4 = [10728475, 7731870]
thread_8 = [11389458, 9105485]
thread_16 = [13904737, 12027593]

# Setting up the bar width and positions
x = np.arange(len(allocators))
width = 0.25

# Plotting the data
fig, ax = plt.subplots(figsize=(8, 6))
bars1 = ax.bar(x - width, thread_4, width, label="Thread Count = 4")
bars2 = ax.bar(x, thread_8, width, label="Thread Count = 8")
bars3 = ax.bar(x + width, thread_16, width, label="Thread Count = 16")

# Adding labels, title, and legend
#ax.set_xlabel("Allocator", fontsize=12)
ax.set_ylabel("Clock Cycles", fontsize=12)
#ax.set_title("Clock Cycle Benchmark for Mempool Allocators", fontsize=14)
ax.set_xticks(x)
ax.set_xticklabels(allocators, rotation=15, ha="right", fontsize=10)
ax.legend()

# Adding value labels to the bars
def add_labels(bars):
    for bar in bars:
        height = bar.get_height()
        ax.text(
            bar.get_x() + bar.get_width() / 2,
            height + 10**5,
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