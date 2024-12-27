import glob
import json
import re
from collections import OrderedDict
import subprocess

# Function to check if a character is a control character (excluding newline and carriage return)
def is_control_char(char):
    return (ord(char) < 32 or ord(char) == 127) and char not in ['\n', '\r']

# Function to parse the processed content into a dictionary
def parse_content_to_dict(content, json_id):
    data = OrderedDict({
        'id': json_id,
        'input': '',
        'label': '',
        'round': ''
    })
    lines = content.splitlines()
    input_lines = []
    
    start_found = False
    label_line = None

    for line in lines:
        if 'Start game' in line:
            start_found = True
        elif start_found and line.startswith('P1'):
            label_line = line
        elif start_found and 'cards_remaining:' in line:
            data['round'] = line.split('cards_remaining: ')[1].strip()
        
        if start_found:
            input_lines.append(line)
    
    # Set input and label fields if the label line was found
    if label_line:
        data['input'] = ' \n '.join(input_lines[:input_lines.index(label_line)]).strip()
        data['input'] += f" \n Considering the hints and actions so far, what move will P1 likely make next?"
        data['label'] = label_line.strip()

    return data if data['input'] and data['label'] else None

# Function to process a single file and return a list of JSON objects
def process_file(file_path, start_id):
    with open(file_path, 'r') as file:
        content = file.read()

    # Replace control characters in content
    content = ''.join(str(ord(char)) if is_control_char(char) else char for char in content)

    # Split the content into individual samples based on 'Start game'
    samples = re.split(r'(Start game)', content)

    json_objects = []
    for i in range(1, len(samples), 2):  # Iterate over pairs of 'Start game' and the following content
        sample = samples[i] + samples[i + 1]
        result = parse_content_to_dict(sample, start_id + len(json_objects) + 1)
        
        if result:
            json_objects.append(result)
    
    return json_objects

# Function to convert json objects to system-user-assistant format 
def convert_to_fine_tune_format(data):
    fine_tune_data = []
    
    for item in data:
        system_message = {
            "role": "system",
            "content": "You are a helpful assistant in the card game of Hanabi."
        }
        
        user_message = {
            "role": "user",
            "content": item['input']
        }
        
        assistant_message = {
            "role": "assistant",
            "content": item['label']
        }
        
        # Create the final structure
        fine_tune_data.append({
            "messages": [system_message, user_message, assistant_message]
        })
    
    return fine_tune_data

# Function to run a bash command and capture its output to a file
def run_command(command, output_file):
    with open(output_file, 'w') as file:
        result = subprocess.run(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        file.write(result.stdout)
        file.write(result.stderr)

# -------------------------------------------------------------------------------------------------------------------------------------------------------------------

# Define the list of bots
bots = ['SimpleBot', 'ValueBot', 'HolmesBot', 'SmartBot', 'InfoBot', 'CheatBot'] 

# Run each command and capture its output
for bot1 in bots:
    for bot2 in bots: 
        command = f'unbuffer python eval_bot.py {bot1} {bot2}  --games 7800 --qa 1' # aiming for 280k samples total
        output_file = f'output_no_qa_2/output_no_qa_2_{bot1}_{bot2}.txt'
        run_command(command, output_file)
        print(f"Executed command '{command}' and saved output to '{output_file}'.")

# Testing 
# command = f'unbuffer python eval_bot.py SmartBot SmartBot  --games 5 --qa 1'
# output_file = f'output_no_qa/TEST_output_no_qa_SmartBot_SmartBot.txt'
# run_command(command, output_file)
# print(f"Executed command '{command}' and saved output to '{output_file}'.")

# Process each file and collect all JSON objects
all_json_objects = []
for input_file_path in glob.glob('output_no_qa_2/output_*.txt'):
    start_id = len(all_json_objects)
    json_objects = process_file(input_file_path, start_id)
    all_json_objects.extend(json_objects)
    print(f"Processed '{input_file_path}'.")

# Fine-tuning format of certain models
all_json_objects = convert_to_fine_tune_format(all_json_objects)

# Write all JSON objects to a single output file
with open('output_no_qa_2/output.json', 'w') as json_file:
    json.dump(all_json_objects, json_file, indent=4)

print(f"Saved all results to 'output_no_qa/output.json'.")
