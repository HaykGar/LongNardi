import numpy as np
import utils

# Load the file back into a NumPy object array
loaded_arrays_obj = np.load('rand_wins.npy', allow_pickle=True)

# Convert the NumPy object array back to a Python list
loaded_arrays_list = loaded_arrays_obj.tolist()

# You can now access and use the loaded arrays
for move, position in enumerate(loaded_arrays_list[0]):
    print(f"move {move}, position: ")
    utils.print_board(position)
