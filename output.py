import glob
import json
import re
import subprocess
from collections import OrderedDict

# Function to check if a character is a control character (excluding newline and carriage return)
def is_control_char(char):
    return (ord(char) < 32 or ord(char) == 127) and char not in ['\n', '\r']

# Function to parse the processed content into a dictionary
def parse_content_to_dict(content):
    data = OrderedDict({
        'botname': '',
        'question': '',
        'answer': '',
        'cards_remaining': '',
        'question_position': '',
        'question_value': ''
    })
    lines = content.splitlines()
    question_lines = []
    for line in lines:
        if 'Final score' in line: 
            continue
        elif 'botname:' in line: 
            data['botname'] = line.split('botname: ')[1].strip()
        elif 'answer:' in line:
            data['answer'] = line.split('answer: ')[1].strip()
        elif 'cards_remaining:' in line:
            data['cards_remaining'] = line.split('cards_remaining: ')[1].strip()
        elif 'question_position:' in line:
            data['question_position'] = line.split('question_position: ')[1].strip()
        elif 'question_value:' in line:
            data['question_value'] = line.split('question_value: ')[1].strip()
        else:
            question_lines.append(line)
    
    data['question'] = '\n'.join(question_lines).strip()
    return data

# Function to process a single file and return a list of JSON objects
def process_file(file_path, json_id):
    with open(file_path, 'r') as file:
        content = file.read()

    # Replace control characters in content
    content = ''.join(str(ord(char)) if is_control_char(char) else char for char in content)

    # Split the content into individual samples
    samples = re.split(r'(Start game)', content)

    json_objects = []
    for i in range(1, len(samples), 2):  # Iterate over pairs of 'Start game' and the following content
        sample = samples[i] + samples[i + 1]
        start_index = sample.find('Start')
        end_index = sample.find('Final score')
        if start_index != -1 and end_index != -1:
            result = sample[start_index:end_index + len('Final score')]
            parsed_data = parse_content_to_dict(result)
            # Check for the presence of all required fields as some complete games do not get to the Q&A logic
            if all(parsed_data[field] not in [None, ''] for field in ['answer', 'cards_remaining', 'question_position', 'question_value']):
                # Add ID attribute at the beginning of the dictionary
                parsed_data = OrderedDict([('id', json_id + i//2)] + list(parsed_data.items()))
                json_objects.append(parsed_data)
    
    return json_objects

# Function to run a bash command and capture its output to a file
def run_command(command, output_file):
    with open(output_file, 'w') as file:
        result = subprocess.run(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        file.write(result.stdout)
        file.write(result.stderr)

# -------------------------------------------------------------------------------------------------------------------------------------------------------------------

bots = ['SimpleBot', 'ValueBot', 'HolmesBot', 'SmartBot', 'InfoBot', 'CheatBot'] 
# Run each command and capture its output
for bot in bots:
    for i in range(2,5): 
        command = f'unbuffer python eval_bot.py {bot} --players 2 --games 38500 --qa 1'
        output_file = f'output_{bot}_{i-1}.txt'
        run_command(command, output_file)
        print(f"Executed command '{command}' and saved output to '{output_file}'.")

# Process each file and collect all JSON objects
all_json_objects = []
for input_file_path in glob.glob('output_*.txt'):
    json_id = len(all_json_objects)
    json_objects = process_file(input_file_path, json_id)
    all_json_objects.extend(json_objects)
    print(f"Processed '{input_file_path}'.")

# Write all JSON objects to a single output file
with open('output.json', 'w') as json_file:
    json.dump(all_json_objects, json_file, indent=4)

print(f"Saved all results to 'output.json'.")