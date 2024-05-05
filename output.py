#Fix issue with ASCII characters in output files
import glob

# Function to check if a character is a control character (excluding newline and carriage return)
def is_control_char(char):
    return (ord(char) < 32 or ord(char) == 127) and char not in ['\n', '\r']

# Process each file that matches the pattern
for input_file_path in glob.glob('*output*.txt'):
    # Initialize the result string
    result = ''

    # Flags to control processing
    start_processing = False
    end_processing = False

    # Read and process the file line by line
    with open(input_file_path, 'r') as file:
        for line in file:
            if 'Start' in line:
                start_processing = True

            if start_processing and not end_processing:
                for char in line:
                    if is_control_char(char):
                        result += str(ord(char))
                    else:
                        result += char

            if 'Final score' in line:
                end_processing = True

            if end_processing:
                break

    # Write the processed content to the output file
    with open(input_file_path, 'w') as file:
        file.write(result)

    print(f"Processed '{input_file_path}' and saved the result to '{input_file_path}'.")
