with open("output_file1", "w") as f:
    for i in range(10):
        # Simple example: binary vectors
        vector = bin(i)[2:].zfill(8) # 8-bit vectors
        f.write(f"{vector}\n")