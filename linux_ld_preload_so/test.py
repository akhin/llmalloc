import random
import string

def stress_memory():
    # Create and manipulate a large list
    large_list = [random.randint(0, 100000) for _ in range(100000)]
    for i in range(0, len(large_list), 1000):
        large_list[i] *= 2

    # Create and manipulate a large dictionary
    large_dict = {f"key_{i}": random.randint(0, 100000) for i in range(100000)}
    for key in list(large_dict.keys())[:50000]:
        large_dict[key] = large_dict[key] + 1

    # Allocate and manipulate large strings
    large_string = ''.join(random.choices(string.ascii_letters + string.digits, k=1000000))
    modified_string = large_string.replace("a", "A")

    # Perform lots of small string concatenations
    small_strings = []
    for i in range(10000):
        small_strings.append("string_" + str(i))
    combined_string = ''.join(small_strings)

    # Create nested lists to simulate complex structures
    nested_list = [[j for j in range(100)] for _ in range(1000)]
    flattened_list = [item for sublist in nested_list for item in sublist]

    # Perform repeated list resizing
    temp_list = []
    for i in range(100000):
        temp_list.append(i)
        if len(temp_list) > 5000:
            temp_list = temp_list[2500:]

if __name__ == "__main__":
    stress_memory()
