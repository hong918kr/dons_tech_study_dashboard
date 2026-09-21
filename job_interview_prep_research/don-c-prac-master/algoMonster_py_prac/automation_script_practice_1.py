import os
import re

def analyze_simulation_log(log_file_path, error_pattern=r"ERROR:\s+(.*)"):
    """
        Analyzes a simulation log file, extracts error messages, and counts them

        Args:
            log_file_path (str) : Path to the simulation log file
            eerror_pattern (str) : Regular expression pattern to match error message

        Returns:
            tuple: A tuple containing the number of errors and a list of error messages.
    """

    try:
        with open(log_file_path, 'r') as log_file:
            log_content = log_file.read()
        error_matches = re.findall(error_pattern, log content)
        error_count = len(error_matches)
        return error_count, error_matches

    except FileNotFoundError:
        return 0, ["Error: Log file not found."]
    except Exception as e:
        return 0, [f"An unexpected error occurred: {e}"]



def generate_test_vectors(num_vectors, output_file="test_vector.txt"):
    