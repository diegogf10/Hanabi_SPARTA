# Complete Hanabi Encoding System Implementation

# Reserved integers (1-10) for special actions
RESERVED_INTEGERS = {
    'P0_DRAW': 1,  # Using reserved integer 1 for P0 draw
    # Remaining reserved integers 2-10 available for future use
}

# Special tokens
SPECIAL_TOKENS = {
    'START': '<|START|>',
    'HAND_P1': '<|HAND_P1|>',
    'DECK_40': '<|DECK_40|>',
    'DECK_0': '<|DECK_0|>',
    'START_MOVES': '<|START_MOVES|>',
    'FINAL_SCORE': '<|FINAL_SCORE_{final_score}|>'  # formatted with score
}

# Card mapping (integers 11-35)
CARD_MAPPING = {
    card: idx + 11 for idx, card in enumerate([
        '1r', '2r', '3r', '4r', '5r',
        '1y', '2y', '3y', '4y', '5y',
        '1g', '2g', '3g', '4g', '5g',
        '1b', '2b', '3b', '4b', '5b',
        '1w', '2w', '3w', '4w', '5w'
    ])
}
# Reverse mapping for validation
REVERSE_CARD_MAPPING = {v: k for k, v in CARD_MAPPING.items()}

POSITIONS = ['O', 'SO', 'M', 'SN', 'N']
COLORS = ['r', 'y', 'g', 'b', 'w']
NUMBERS = ['1', '2', '3', '4', '5']

# Moves for P0 (You): Play (success/fail), discard, hint (36-720)
MOVE_BASE_P0 = 36

# Moves for P1: Play (success/fail), discard, hint, draw known (721+)
MOVE_BASE_P1 = 721
DRAW_BASE_P1 = 1406

# Helper functions for encoding with validation

def validate_card(card):
    """Validate that a card string is in the correct format."""
    if not isinstance(card, str) or len(card) != 2:
        raise ValueError(f"Invalid card format: {card}, expected format like '1r'")
    
    value, color = card[0], card[1]
    if value not in NUMBERS or color not in COLORS:
        raise ValueError(f"Invalid card: {card}, value must be in {NUMBERS} and color in {COLORS}")
    
    return True

def validate_position(pos):
    """Validate that a position is valid."""
    if pos not in POSITIONS:
        raise ValueError(f"Invalid position: {pos}, must be one of {POSITIONS}")
    
    return True

def encode_play(player_base, pos, card, outcome='success'):
    """Encode a play action with validation."""
    validate_position(pos)
    validate_card(card)
    
    if outcome not in ['success', 'fail']:
        raise ValueError(f"Invalid outcome: {outcome}, must be 'success' or 'fail'")
    
    pos_idx = POSITIONS.index(pos)
    card_idx = CARD_MAPPING[card] - 11
    outcome_offset = 0 if outcome == 'success' else 125
    return player_base + pos_idx * 25 + card_idx + outcome_offset

def encode_discard(player_base, pos, card):
    """Encode a discard action with validation."""
    validate_position(pos)
    validate_card(card)
    
    pos_idx = POSITIONS.index(pos)
    card_idx = CARD_MAPPING[card] - 11
    return player_base + 250 + pos_idx * 25 + card_idx

def encode_hint(player_base, pos_list, hint_type, hint_value):
    """Encode a hint action with validation."""
    # Validate positions
    if not pos_list:
        raise ValueError("Position list cannot be empty for a hint")
    
    for pos in pos_list:
        validate_position(pos)
    
    # Validate hint type and value
    if hint_type not in ['color', 'number']:
        raise ValueError(f"Invalid hint type: {hint_type}, must be 'color' or 'number'")
    
    if hint_type == 'color' and hint_value not in COLORS:
        raise ValueError(f"Invalid color hint: {hint_value}, must be one of {COLORS}")
    elif hint_type == 'number' and hint_value not in NUMBERS:
        raise ValueError(f"Invalid number hint: {hint_value}, must be one of {NUMBERS}")
    
    pos_code = sum(2**POSITIONS.index(p) for p in pos_list)
    hint_offset = COLORS.index(hint_value) if hint_type == 'color' else 5 + NUMBERS.index(hint_value)
    return player_base + 375 + pos_code * 10 + hint_offset

def encode_p1_draw(card):
    """Encode P1 drawing a specific card with validation."""
    validate_card(card)
    return DRAW_BASE_P1 + CARD_MAPPING[card] - 11

#Main encoding function
def encode_transcript(transcript):
    """Encode a game transcript with input validation and error handling."""
    encoded_sequence = []
    initial_hand_encoded = False
    
    if not transcript or not isinstance(transcript, str):
        raise ValueError("Transcript must be a non-empty string")

    lines = transcript.strip().split('\n')
    
    for line_num, line in enumerate(lines, 1):
        try:
            line = line.strip()
            
            if not line:
                continue

            # Check for final score in the line first
            final_score_part = None
            if "Final score" in line and not line.startswith("Final score"):
                parts = line.split("Final score")
                line = parts[0].strip()  # Keep only the move part for further processing
                final_score_part = "Final score" + parts[1].strip()
            
            # Process the move part as normal
            if line.startswith("Start game"):
                encoded_sequence.append(SPECIAL_TOKENS['START'])

            elif line.startswith("40 cards remaining"):
                encoded_sequence.append(SPECIAL_TOKENS['DECK_40'])

            elif line.startswith("0 Cards Remaining"):
                encoded_sequence.append(SPECIAL_TOKENS['DECK_0'])

            elif line.startswith("Hands: P1 cards are") and not initial_hand_encoded:
                encoded_sequence.append(SPECIAL_TOKENS['HAND_P1'])
                hand_str = line.replace("Hands: P1 cards are", "").replace(",", " ").replace(";","").strip()
                if not hand_str:
                    raise ValueError(f"Line {line_num}: Invalid P1 hand format")
                    
                hand = hand_str.split()
                encoded_hand = []
                for card in hand:
                    validate_card(card)
                    encoded_hand.append(CARD_MAPPING[card])
                encoded_sequence.extend(encoded_hand)
                encoded_sequence.append(SPECIAL_TOKENS['START_MOVES'])
                initial_hand_encoded = True

            elif line.startswith("You played"):
                if "your" not in line or "card" not in line or "(" not in line or ")" not in line:
                    raise ValueError(f"Line {line_num}: Invalid play format: {line}")
                    
                pos = line.split("your")[1].split("card")[0].strip()
                validate_position(pos)
                
                card_match = line.split('(')[1].split(')')[0].strip()
                validate_card(card_match)
                
                outcome = 'fail' if 'failed' in line else 'success'
                encoded_sequence.append(encode_play(MOVE_BASE_P0, pos, card_match, outcome))
                
                if "You drew a card" in line:
                    encoded_sequence.append(RESERVED_INTEGERS['P0_DRAW'])

            elif line.startswith("You discarded"):
                if "your" not in line or "card" not in line or "(" not in line or ")" not in line:
                    raise ValueError(f"Line {line_num}: Invalid discard format: {line}")
                    
                pos = line.split("your")[1].split("card")[0].strip()
                validate_position(pos)
                
                card_match = line.split('(')[1].split(')')[0].strip()
                validate_card(card_match)
                
                encoded_sequence.append(encode_discard(MOVE_BASE_P0, pos, card_match))
                
                if "You drew a card" in line:
                    encoded_sequence.append(RESERVED_INTEGERS['P0_DRAW'])

            elif line.startswith("P1 played"):
                if "his" not in line or "card" not in line or "(" not in line or ")" not in line:
                    raise ValueError(f"Line {line_num}: Invalid P1 play format: {line}")
                    
                pos = line.split("his")[1].split("card")[0].strip()
                validate_position(pos)
                
                card_match = line.split('(')[1].split(')')[0].strip()
                validate_card(card_match)
                
                outcome = 'fail' if 'failed' in line else 'success'
                encoded_sequence.append(encode_play(MOVE_BASE_P1, pos, card_match, outcome))
                
                if "P1 drew card" in line:
                    drawn_part = line.split("P1 drew card")[1].strip()
                    if not drawn_part:
                        raise ValueError(f"Line {line_num}: Missing P1 drawn card")
                    encoded_sequence.append(encode_p1_draw(drawn_part))

            elif line.startswith("P1 discarded"):
                if "his" not in line or "card" not in line or "(" not in line or ")" not in line:
                    raise ValueError(f"Line {line_num}: Invalid P1 discard format: {line}")
                    
                pos = line.split("his")[1].split("card")[0].strip()
                validate_position(pos)
                
                card_match = line.split('(')[1].split(')')[0].strip()
                validate_card(card_match)
                
                encoded_sequence.append(encode_discard(MOVE_BASE_P1, pos, card_match))
                
                if "P1 drew card" in line:
                    drawn_part = line.split("P1 drew card")[1].strip()
                    if not drawn_part:
                        raise ValueError(f"Line {line_num}: Missing P1 drawn card")
                    encoded_sequence.append(encode_p1_draw(drawn_part))

            elif "You told P1" in line or "P1 told You" in line:
                player_base = MOVE_BASE_P0 if "You told P1" in line else MOVE_BASE_P1
                split_keyword = "his" if player_base == MOVE_BASE_P0 else "your"
                
                if split_keyword not in line:
                    raise ValueError(f"Line {line_num}: Invalid hint format: {line}")
                    
                hint_segment = line.split(split_keyword)[1].strip()
                
                if 'card is' in hint_segment:
                    pos_str, hint_part = hint_segment.split('card is')
                elif 'cards are' in hint_segment:
                    pos_str, hint_part = hint_segment.split('cards are')
                else:
                    raise ValueError(f"Line {line_num}: Cannot parse hint: {hint_segment}")
                
                pos_list = [pos.strip() for pos in pos_str.strip().replace(",", " ").split()]
                if not pos_list:
                    if "all" in line:
                        pos_list = POSITIONS
                    else:
                        print(line)
                        raise ValueError(f"Line {line_num}: No positions found in hint")
                
                for pos in pos_list:
                    validate_position(pos)
                    
                hint_part = hint_part.strip().replace("a ", "")
                
                if hint_part in COLORS:
                    hint_type = 'color'
                elif hint_part in NUMBERS:
                    hint_type = 'number'
                else:
                    raise ValueError(f"Line {line_num}: Invalid hint value: {hint_part}")
                    
                encoded_sequence.append(encode_hint(player_base, pos_list, hint_type, hint_part))

            elif line.startswith("Final score"):
                # Extract the score using regex to be more robust
                import re
                score_matches = re.findall(r'\d+', line)
                if len(score_matches) >= 2:
                    # If there are multiple numbers like "8 : 18", use the second one
                    final_score = int(score_matches[1])
                elif len(score_matches) == 1:
                    # If there's only one number
                    final_score = int(score_matches[0])
                else:
                    raise ValueError(f"Line {line_num}: No score found in: {line}")
                    
                encoded_sequence.append(SPECIAL_TOKENS['FINAL_SCORE'].format(final_score=final_score))

            elif line.startswith("Hands:") or line.startswith("Piles") or line.startswith("bomb"):
                # These lines are skipped in the encoding
                continue
                
            # Now handle the final score part if it was split from the original line
            if final_score_part:
                # Extract the score using regex to be more robust
                import re
                score_matches = re.findall(r'\d+', final_score_part)
                if len(score_matches) >= 2:
                    # If there are multiple numbers like "8 : 18", use the second one
                    final_score = int(score_matches[1])
                elif len(score_matches) == 1:
                    # If there's only one number
                    final_score = int(score_matches[0])
                else:
                    raise ValueError(f"Line {line_num}: No score found in: {final_score_part}")
                    
                encoded_sequence.append(SPECIAL_TOKENS['FINAL_SCORE'].format(final_score=final_score))
                
        except Exception as e:
            raise ValueError(f"Error at line {line_num}: {str(e)}")

    # Final validation - make sure we have a final score token
    if not any(token.startswith('<|FINAL_SCORE_') for token in encoded_sequence if isinstance(token, str)):
        raise ValueError("Missing final score in game transcript")

    return encoded_sequence

# Example use:
if __name__ == "__main__":
    try:
        # Sample transcript for testing
        transcript_example = """Start game
40 cards remaining
Hands: P1 cards are 1y,2g,1g,2y,2w;
You told P1 his O, M cards are a 1
P1 played his O card (1y). P1 drew card 1g
Piles: 0r 0w 1y 0g 0b
Hands: P1 cards are 2g,1g,2y,2w,1g;
You told P1 his O, SO, N cards are g
P1 played his SO card (1g). P1 drew card 4w
Piles: 0r 0w 1y 1g 0b
Hands: P1 cards are 2g,2y,2w,1g,4w;
You told P1 his O, SO, M cards are a 2
P1 played his O card (2g). P1 drew card 1y
Piles: 0r 0w 1y 2g 0b
Hands: P1 cards are 2y,2w,1g,4w,1y;
You told P1 his O, N cards are y
P1 played his O card (2y). P1 drew card 1r
Piles: 0r 0w 2y 2g 0b
Hands: P1 cards are 2w,1g,4w,1y,1r;
You told P1 his SO, SN, N cards are a 1
P1 played his N card (1r). P1 drew card 1w
Piles: 1r 0w 2y 2g 0b
Hands: P1 cards are 2w,1g,4w,1y,1w;
You told P1 his O, M, N cards are w
P1 told You your SO card is g
You told P1 his O, M, N cards are w
P1 played his N card (1w). P1 drew card 2r
Piles: 1r 1w 2y 2g 0b
Hands: P1 cards are 2w,1g,4w,1y,2r;
You discarded your O card (2b). You drew a card
P1 played his O card (2w). P1 drew card 3y
Piles: 1r 2w 2y 2g 0b
Hands: P1 cards are 1g,4w,1y,2r,3y;
You told P1 his SN card is r
P1 played his SN card (2r). P1 drew card 1y
Piles: 2r 2w 2y 2g 0b
Hands: P1 cards are 1g,4w,1y,3y,1y;
You discarded your O card (3g). You drew a card
P1 told You your O, SO, N cards are r
You played your N card (3r). You drew a card
Piles: 3r 2w 2y 2g 0b
P1 discarded his O card (1g). P1 drew card 4y
Hands: P1 cards are 4w,1y,3y,1y,4y;
You discarded your O card (5r). You drew a card
P1 told You your SN card is w
You discarded your O card (1r). You drew a card
P1 told You your M card is a 3
You played your M card (3w). You drew a card
Piles: 3r 3w 2y 2g 0b
P1 told You your O, SO, N cards are y
You played your N card (3y). You drew a card
Piles: 3r 3w 3y 2g 0b
P1 discarded his SO card (1y). P1 drew card 3r
Hands: P1 cards are 4w,3y,1y,4y,3r;
You told P1 his O card is w
P1 played his O card (4w). P1 drew card 1g
Piles: 3r 4w 3y 2g 0b
Hands: P1 cards are 3y,1y,4y,3r,1g;
You discarded your N card (3b). You drew a card
P1 discarded his N card (1g). P1 drew card 4w
Hands: P1 cards are 3y,1y,4y,3r,4w;
You told P1 his O, SO, M cards are y
P1 discarded his O card (3y). P1 drew card 2b
Hands: P1 cards are 1y,4y,3r,4w,2b;
You told P1 his SO, SN cards are a 4
P1 played his SO card (4y). P1 drew card 4b
Piles: 3r 4w 4y 2g 0b
Hands: P1 cards are 1y,3r,4w,2b,4b;
You told P1 his O card is a 1
P1 discarded his O card (1y). P1 drew card 1r
Hands: P1 cards are 3r,4w,2b,4b,1r;
You told P1 his N card is a 1
P1 discarded his SO card (4w). P1 drew card 2g
Hands: P1 cards are 3r,2b,4b,1r,2g;
You told P1 his SN card is a 1
P1 discarded his SN card (1r). P1 drew card 4r
Hands: P1 cards are 3r,2b,4b,2g,4r;
You told P1 his O, N cards are r
P1 played his N card (4r). P1 drew card 2r
Piles: 4r 4w 4y 2g 0b
Hands: P1 cards are 3r,2b,4b,2g,2r;
You discarded your M card (4b). You drew a card
P1 discarded his O card (3r). P1 drew card 3w
Hands: P1 cards are 2b,4b,2g,2r,3w;
You told P1 his O, M, SN cards are a 2
P1 discarded his SN card (2r). P1 drew card 1w
Hands: P1 cards are 2b,4b,2g,3w,1w;
You told P1 his SN, N cards are w
P1 discarded his O card (2b). P1 drew card 5g
Hands: P1 cards are 4b,2g,3w,1w,5g;
You told P1 his SO, N cards are g
P1 discarded his SO card (2g). P1 drew card 3g
Hands: P1 cards are 4b,3w,1w,5g,3g;
You told P1 his SO, N cards are a 3
P1 discarded his SO card (3w). P1 drew card 1b
Hands: P1 cards are 4b,1w,5g,3g,1b;
You told P1 his M, SN cards are g
P1 played his SN card (3g). P1 drew card 3b
Piles: 4r 4w 4y 3g 0b
Hands: P1 cards are 4b,1w,5g,1b,3b;
You discarded your O card (5y). You drew a card
P1 told You your SO, N cards are w
You discarded your O card (2y). You drew a card
P1 told You your SO, M cards are a 4
You discarded your SO card (4g). You drew a card
P1 told You your M card is a 5
You played your M card (5w). You drew a card
Piles: 4r 5w 4y 3g 0b
P1 told You your M, N cards are b
You discarded your O card (2w). You drew a card
P1 told You your SO, SN cards are a 1
You played your SO card (1b). You drew a card
Piles: 4r 5w 4y 3g 1b
P1 told You your SN card is g
You played your SN card (4g). You drew a card
Piles: 4r 5w 4y 4g 1b
0 Cards Remaining
P1 discarded his O card (4b). Hands: P1 cards are 1w,5g,1b,3b;
You played your M card (1b) but failed. 
Final score 8 : 18 bomb: 0"""

        encoded_game = encode_transcript(transcript_example)
        print("Encoded game:", encoded_game)
        print(f"Successfully encoded the transcript with {len(encoded_game)} tokens")
        
    except ValueError as e:
        print(f"Validation error: {e}")
    except Exception as e:
        print(f"Unexpected error: {str(e)}")
