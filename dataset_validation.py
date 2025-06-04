import json
import re
from collections import Counter, defaultdict

# Load JSON data
def load_data(filename):
    with open(filename, 'r') as file:
        return json.load(file)

# Function to parse action type and its corresponding parts
def parse_actions(content):
    actions = []
    parts = content.split(';')
    for part in parts:
        if part.startswith(('play', 'discard', 'hint')):
            actions.append(part)
    return actions

# Analysis function
def analyze_dataset(json_data):
    action_counts = Counter()
    position_counts = defaultdict(Counter)
    number_counts = defaultdict(Counter)
    color_counts = defaultdict(Counter)

    number_of_actions = 0

    valid_positions = ('SO', 'SN', 'O', 'M', 'N')  # Order matters to avoid substring matching

    for sample in json_data:
        for message in sample['messages']:
            if message['role'] == 'assistant':
                actions = parse_actions(message['content'])
                for action in actions:
                    parts = action.split('|')
                    act_type = parts[0]
                    action_counts[act_type] += 1
                    number_of_actions += 1

                    # Positions
                    positions_part = parts[2].split(',')
                    for pos in positions_part:
                        if pos in valid_positions:
                            position_counts[act_type][pos] += 1
                            break  # To avoid double counting positions

                    # Number or Color
                    card_part = parts[3] if len(parts) > 3 else ''
                    if act_type == 'hint':
                        if card_part in ('r', 'b', 'g', 'y', 'w'):
                            color_counts[act_type][card_part] += 1
                        elif card_part in ('1', '2', '3', '4', '5'):
                            number_counts[act_type][card_part] += 1
                    else:
                        match = re.match(r'(\d)([rbgyw])', card_part)
                        if match:
                            number, color = match.groups()
                            number_counts[act_type][number] += 1
                            color_counts[act_type][color] += 1

    # Output results
    print("Total Actions:", number_of_actions)

    # Proportion of actions
    print("\nAction Proportions:")
    for action, count in action_counts.items():
        print(f"{action}: {count / number_of_actions:.2%}")

    # Positions proportion
    print("\nPositions proportion by action:")
    for action, positions in position_counts.items():
        total = sum(positions.values())
        print(f"\nAction: {action}")
        for position, count in positions.items():
            print(f"  {position}: {count / total:.2%}")

    # Numbers proportion
    print("\nNumbers proportion by action:")
    for action, numbers in number_counts.items():
        total = sum(numbers.values())
        print(f"\nAction: {action}")
        for number, count in numbers.items():
            print(f"  {number}: {count / total:.2%}")

    # Colors proportion
    print("\nColors proportion by action:")
    for action, colors in color_counts.items():
        total = sum(colors.values())
        print(f"\nAction: {action}")
        for color, count in colors.items():
            print(f"  {color}: {count / total:.2%}")

# Example Usage
filename = 'training_samples_extended.json'
data = load_data(filename)
analyze_dataset(data)
