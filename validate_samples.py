import json
import re
from typing import List, Dict, Any, Tuple
import argparse
from collections import Counter

def analyze_content_format(content: str) -> str:
    """Analyze the format of the content to help debug parsing issues."""
    newline_count = content.count('\n')
    comma_count = content.count(',')
    start_token_count = content.count('<|START|>')
    
    format_details = []
    if newline_count >= 4:
        format_details.append(f"Content has {newline_count} newlines, likely line-separated games")
    elif comma_count > 0 and start_token_count >= 5:
        format_details.append(f"Content has {start_token_count} <|START|> tokens within a comma-separated sequence")
    else:
        format_details.append("Content format unclear")
    
    return "; ".join(format_details)

def parse_game_sequence(sequence_str: str) -> List[Any]:
    """Parse a comma-separated string of tokens into a list of integers and special tokens."""
    elements = []
    for element in sequence_str.split(','):
        element = element.strip()
        if not element:
            continue
        if element.startswith('<') and element.endswith('>'):
            # Special token
            elements.append(element)
        else:
            # Integer
            try:
                elements.append(int(element))
            except ValueError:
                print(f"Warning: Could not parse element as integer: {element}")
                elements.append(element)  # Keep as string for reporting
    return elements

def extract_games(content: str) -> List[List[Any]]:
    """Extract individual games from a multi-game string content."""
    games = []
    current_game = []
    
    # Split by newlines first, then process each line
    lines = content.strip().split('\n')
    
    for line in lines:
        line_elements = parse_game_sequence(line)
        if not line_elements:
            continue
            
        # Each line should represent a complete game
        games.append(line_elements)
    
    # If only one line and no newline separation, try to detect games by <START> token
    if len(games) == 1 and len(games[0]) > 0:
        elements = games[0]
        games = []
        current_game = []
        
        for element in elements:
            if element == '<|START|>' and current_game:
                games.append(current_game)
                current_game = [element]
            else:
                current_game.append(element)
        
        if current_game:
            games.append(current_game)
    
    return games

def validate_game_structure(game: List[Any]) -> Tuple[bool, str]:
    """Validate that a game follows the expected structure."""
    # Check game starts with <|START|>
    if not game or game[0] != '<|START|>':
        return False, "Game does not start with <|START|>"
    
    # Check if game has deck information
    if not any(str(token).startswith('<|DECK_') for token in game):
        return False, "Game does not contain deck information"
    
    # Check if complete games end with a final score
    final_score_pattern = re.compile(r'\<\|FINAL_SCORE_\d+\|\>')
    has_final_score = any(isinstance(token, str) and final_score_pattern.match(token) for token in game)
    
    # Special check for the last game which might be partial
    has_moves = any(isinstance(token, int) and 36 <= token < 1200 for token in game)
    
    if not has_moves:
        return False, "Game does not contain any moves"
    
    # For complete games, ensure there's a final score
    if not has_final_score and '<|DECK_0|>' in game:
        return False, "Game appears to be complete (has <|DECK_0|>) but no final score"
        
    return True, "Valid"

def validate_move_ranges(game: List[Any]) -> Tuple[bool, str]:
    """Validate that moves in the game are in the expected ranges."""
    invalid_moves = []
    
    for item in game:
        if isinstance(item, int):
            # Card: 11-35
            # P0 draw: 1
            # P0 move: 36-720
            # P1 move: 721-1405
            # P1 draw: 1406+
            if not (item == 1 or (11 <= item <= 35) or (36 <= item <= 720) or (721 <= item <= 1405) or (item >= 1406)):
                invalid_moves.append(item)
    
    if invalid_moves:
        return False, f"Game contains invalid move codes: {invalid_moves}"
    
    return True, "Valid"

def check_last_move(game: List[Any]) -> Tuple[bool, str]:
    """Check that the last move in a game is appropriate."""
    last_move = None
    
    # Find the last numeric element
    for item in reversed(game):
        if isinstance(item, int):
            last_move = item
            break
    
    if last_move is None:
        return False, "No moves found in game"
    
    # For partial games, the last move should be by P0 (36-720) or a P0 draw action (1)
    # We're being a bit lenient here as data might have different conventions
    if not (last_move == 1 or (36 <= last_move <= 720)):
        return False, f"Last move is not by P0: {last_move}"
    
    return True, "Valid"

def validate_sample(sample: Dict[str, Any]) -> Tuple[bool, List[str]]:
    """Validate a complete sample including context games and prediction."""
    issues = []
    
    # Extract messages
    if 'messages' not in sample:
        return False, ["Sample does not have 'messages' field"]
    
    messages = sample['messages']
    if len(messages) != 3:
        return False, [f"Expected 3 messages, found {len(messages)}"]
    
    # Check system message
    if messages[0]['role'] != 'system':
        issues.append("First message is not a system message")
    
    # Check user message
    if messages[1]['role'] != 'user':
        issues.append("Second message is not a user message")
    
    # Check assistant message
    if messages[2]['role'] != 'assistant':
        issues.append("Third message is not an assistant message")
    
    # Process user content
    user_content = messages[1]['content']
    games = extract_games(user_content)
    
    # Check we have exactly 5 games (4 context + 1 partial)
    if len(games) != 5:
        issues.append(f"Expected exactly 5 games (4 context + 1 partial), found {len(games)}")
        # If we have too few games, provide more details about what we found
        if len(games) < 5:
            for i, game in enumerate(games):
                start_count = sum(1 for token in game if token == '<|START|>')
                end_count = sum(1 for token in game if isinstance(token, str) and token.startswith('<|FINAL_SCORE_'))
                issues.append(f"  Game {i+1}: Has {start_count} <|START|> tokens and {end_count} final score tokens")
            
            # Add info about the content structure
            tokens_sample = str(games[0][:10]) + "..." if games else "[]"
            issues.append(f"  First game tokens sample: {tokens_sample}")
            issues.append(f"  Content format analysis: {analyze_content_format(user_content)}")
    
    # Validate each game
    for i, game in enumerate(games):
        is_last_game = (i == len(games) - 1)
        
        # Validate game structure
        structure_valid, structure_msg = validate_game_structure(game)
        if not structure_valid:
            issues.append(f"Game {i+1}: {structure_msg}")
        
        # Validate move ranges
        moves_valid, moves_msg = validate_move_ranges(game)
        if not moves_valid:
            issues.append(f"Game {i+1}: {moves_msg}")
        
        # For the last game, check its last move
        if is_last_game:
            last_move_valid, last_move_msg = check_last_move(game)
            if not last_move_valid:
                issues.append(f"Last game: {last_move_msg}")
    
    # Validate prediction label
    prediction = messages[2]['content']
    try:
        prediction_int = int(prediction)
        if not (721 <= prediction_int <= 1405):
            issues.append(f"Prediction {prediction_int} is not in the valid P1 move range (721-1405)")
    except ValueError:
        issues.append(f"Prediction is not an integer: {prediction}")
    
    return len(issues) == 0, issues

def validate_file(filename: str, max_samples: int = None) -> Tuple[int, int, List[Dict[str, Any]]]:
    """Validate all samples in the given JSON file."""
    with open(filename, 'r', encoding='utf-8') as f:
        data = json.load(f)
    
    if max_samples is not None:
        data = data[:max_samples]
    
    total_samples = len(data)
    valid_samples = 0
    invalid_samples = []
    
    for i, sample in enumerate(data):
        print(f"\rValidating sample {i+1}/{total_samples}...", end="")
        
        is_valid, issues = validate_sample(sample)
        if is_valid:
            valid_samples += 1
        else:
            invalid_samples.append({"sample_index": i, "issues": issues})
    
    print(f"\nValidated {total_samples} samples: {valid_samples} valid, {total_samples - valid_samples} invalid")
    return valid_samples, total_samples, invalid_samples

def analyze_predictions(filename: str, max_samples: int = None) -> None:
    """Analyze the distribution of prediction values."""
    with open(filename, 'r', encoding='utf-8') as f:
        data = json.load(f)
    
    if max_samples is not None:
        data = data[:max_samples]
    
    predictions = []
    for sample in data:
        try:
            prediction = int(sample['messages'][2]['content'])
            predictions.append(prediction)
        except (KeyError, ValueError, IndexError):
            continue
    
    # Count occurrences
    counter = Counter(predictions)
    
    # Group by ranges
    ranges = {
        'Play': 0,
        'Discard': 0,
        'Hint': 0,
        'Invalid': 0
    }
    
    # Add specific tracking for P0 draws
    p0_draws = 0
    
    # Based on correct encoding ranges:
    # - 250 different play moves
    # - 125 discard moves
    # - 310 hint moves
    for pred, count in counter.items():
        if pred == 1:  # P0 draw
            p0_draws += count
            ranges['Invalid'] += count  # P0 draws shouldn't be predictions
        elif 721 <= pred <= 970:  # Play range (250 moves)
            ranges['Play'] += count
        elif 971 <= pred <= 1095:  # Discard range (125 moves)
            ranges['Discard'] += count
        elif 1096 <= pred <= 1405:  # Hint range (310 moves)
            ranges['Hint'] += count
        else:
            ranges['Invalid'] += count
    
    if p0_draws > 0:
        print(f"\nWARNING: Found {p0_draws} predictions of P0 draw actions (code 1) which should not be predicted.")
    
    # Print results
    print("\nPrediction Analysis:")
    print(f"Total predictions: {len(predictions)}")
    print("Distribution by move type:")
    for move_type, count in ranges.items():
        percentage = (count / len(predictions)) * 100 if predictions else 0
        print(f"  {move_type}: {count} ({percentage:.2f}%)")
    
    # Print most common predictions
    print("\nTop 10 most common predictions:")
    for pred, count in counter.most_common(10):
        percentage = (count / len(predictions)) * 100 if predictions else 0
        print(f"  {pred}: {count} ({percentage:.2f}%)")

def main():
    parser = argparse.ArgumentParser(description='Validate Hanabi integer-encoded training samples')
    parser.add_argument('filename', help='JSON file containing the samples')
    parser.add_argument('--max', type=int, help='Maximum number of samples to validate')
    parser.add_argument('--analyze', action='store_true', help='Analyze prediction distribution')
    parser.add_argument('--output', help='Output file for detailed validation results')
    
    args = parser.parse_args()
    
    valid_count, total_count, invalid_samples = validate_file(args.filename, args.max)
    
    if args.analyze:
        analyze_predictions(args.filename, args.max)
    
    if args.output and invalid_samples:
        with open(args.output, 'w', encoding='utf-8') as f:
            json.dump(invalid_samples, f, indent=2)
        print(f"Detailed validation results written to {args.output}")
    
    print(f"Validation rate: {(valid_count/total_count)*100:.2f}%")
    
    # Return a success code if all samples are valid
    return 0 if valid_count == total_count else 1

if __name__ == "__main__":
    main()