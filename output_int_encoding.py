import os
import glob
from typing import List, Dict, Tuple
import random
from itertools import islice
from dataclasses import dataclass
from pathlib import Path
import json
from int_encoder import encode_transcript  # Adjust this import as needed

VIEWS_PER_SAMPLE     = 4     # A, B, C, D
GAMES_PER_VIEW       = 4     # 4 complete games in every view
REMOVE_PARTIAL_AFTER = True  # pop partial game once it is used

@dataclass
class TrainingSample:
    context_games: List[List[int]]  # 4 complete games (integer encoded)
    partial_game: List[int]         # Partial game up to prediction point
    prediction_label: int           # Integer representing P1's move to predict
    bot_pair: Tuple[str, str]       # The bots playing in this sample

class TrainingSampleGenerator:
    def __init__(self, full_games_dir: str, prediction_games_dir: str):
        self.full_games_dir = Path(full_games_dir)
        self.prediction_games_dir = Path(prediction_games_dir)
        self.used_games = set()  # Track used complete games

    def _read_and_encode_games(self, filepath: Path) -> List[List[int]]:
        try:
            with open(filepath, 'r', encoding='utf-8') as f:
                content = f.read()
            raw_games = ["Start game\n" + g.strip() for g in content.split('Start game') if g.strip()]
            encoded_games = []
            for game in raw_games:
                try:
                    encoded_game = encode_transcript(game)
                    encoded_games.append(encoded_game)  # Keep this as a list of integers
                except Exception as e:
                    print(f"Error encoding game: {str(e)}")
                    continue
            return encoded_games
        except Exception as e:
            print(f"Error reading file {filepath}: {str(e)}")
            return []

    def _extract_prediction_data(self, encoded_game: List) -> Tuple[List, int]:
        if len(encoded_game) <= 10:
            return None, None
        
        # Filter out only numeric moves that are valid P1 moves
        valid_p1_moves = [move for move in encoded_game 
                        if isinstance(move, int) and 721 <= move < 1406]
        
        if not valid_p1_moves:
            return None, None

        last_valid_p1_move = valid_p1_moves[-1]
        
        # Find the index of this move in the original sequence
        last_move_index = len(encoded_game) - 1 - encoded_game[::-1].index(last_valid_p1_move)

        context = encoded_game[:last_move_index]
        prediction_move = last_valid_p1_move

        return context, prediction_move
    
    def pick_views(self, full_games_pool, num_views, games_per_view, rng):
        """
        Returns list[ list[str] ] length `num_views`,
        each inner list has `games_per_view` strings (complete encoded games).
        Ensures no game is repeated **inside** one sample.
        """
        if len(full_games_pool) < num_views * games_per_view:
            raise ValueError("Not enough full games left for one PoE sample")

        # sample without replacement for this sample
        chosen = rng.sample(full_games_pool, num_views * games_per_view)

        # split into chunks
        views = [
            chosen[i * games_per_view:(i + 1) * games_per_view]
            for i in range(num_views)
        ]
        return views

    def _process_bot_pair(self, bot1, bot2,
                      num_samples: int,
                      mode: str,
                      rng: random.Random) -> list[TrainingSample]:

        samples = []
        # -------- load pools exactly as before -------------
        full_games_file = self.full_games_dir / f"full_games_{bot1}_{bot2}.txt"
        pred_games_file = self.prediction_games_dir / f"prediction_games_{bot1}_{bot2}.txt"
        if not full_games_file.exists() or not pred_games_file.exists():
            return samples

        full_games_pool = self._read_and_encode_games(full_games_file)
        partial_games   = self._read_and_encode_games(pred_games_file)

        # shuffle once for reproducibility
        rng.shuffle(partial_games)

        for partial in partial_games:
            if len(samples) == num_samples:
                break

            context, prediction_move = self._extract_prediction_data(partial)
            if context is None:      # skip bad sample
                continue

            if mode == "baseline":
                # === OLD behaviour: 1 view × 4 games ==================
                if len(full_games_pool) < GAMES_PER_VIEW: break
                view_idx  = rng.sample(range(len(full_games_pool)), GAMES_PER_VIEW)
                view      = [full_games_pool[i] for i in view_idx]

                # remove by descending index so positions stay valid
                for i in sorted(view_idx, reverse=True):
                    full_games_pool.pop(i)


            elif mode == "poe":
                # === NEW behaviour: 4 independent views ================
                views = self.pick_views(full_games_pool,
                                VIEWS_PER_SAMPLE,
                                GAMES_PER_VIEW,
                                rng)
            else:
                raise ValueError(f"Unknown mode {mode}")

            sample = TrainingSample(
                context_games   = views,        # type changed to nested list
                partial_game    = context,
                prediction_label= prediction_move,
                bot_pair        = (bot1, bot2)
            )
            samples.append(sample)

            # optionally remove the partial game so it can't be reused
            if REMOVE_PARTIAL_AFTER:
                # mark by identity:
                partial[:] = []

        return samples


    def _format_sample(self, sample: TrainingSample) -> Dict:
        user_content = '\n'.join(
            [','.join(map(str, game)) for game in sample.context_games] + 
            [','.join(map(str, sample.partial_game))]
        )

        return {
            "messages": [
                {
                    "role": "system",
                    "content": """You are an assistant that specializes in understanding and predicting moves in the card game Hanabi. You've been trained on thousands of Hanabi game transcripts that have been encoded as sequences of integers.
                                In this encoding system:
                                    - Each game starts with the token '<|START|>'
                                    - Each game ends with a token '<|FINAL_SCORE_X|>' where X is the final score
                                    - Deck status is represented as '<|DECK_X|>' where X is the number of cards remaining
                                    - P1's (your partner's) initial hand is listed after the '<|HAND_P1|>' token 
                                    - All game moves begin after the '<|START_MOVES|>' token
                                    - Game actions are encoded in specific integer ranges:
                                        * Cards are encoded as integers 11-35
                                        * Your moves (P0) are encoded as integers 36-720
                                        * Your partner's moves (P1) are encoded as integers 721-1405
                                        * Your partner's card draws are encoded as integers 1406+
                                Your task is to analyze sequences of encoded Hanabi game moves and predict P1's next action based on the pattern of play. The input will contain multiple games for context, with each game separated by a newline. The final sequence represents the current game for which you need to make a prediction.
                                Your response should be a single integer in the range 721-1405 corresponding to P1's most likely next move. Do not provide explanations or additional text, only output the predicted integer."""
                },
                {
                    "role": "user",
                    "content": user_content
                },
                {
                    "role": "assistant",
                    "content": str(sample.prediction_label)
                }
            ]
        }
    
    def _format_sample_poe(self, sample: TrainingSample) -> dict:
    # ---- user content -------------------------------------------------
        user_lines = []
        for idx, view in enumerate(sample.context_games, start=1):
            flat_view = ",".join(map(str, view))
            user_lines.append(f'"fullgames_view_{idx}": "{flat_view}"')
        user_lines.append(f'"partial_game": "{",".join(map(str, sample.partial_game))}"')

        user_json_block = "{\n  " + ",\n  ".join(user_lines) + "\n}"

        return {
            "messages": [
                { "role":"system",
                    "content": """You are an assistant that specializes in understanding and predicting moves in the card game Hanabi. You've been trained on thousands of Hanabi game transcripts that have been encoded as sequences of integers.
                                    In this encoding system:
                                        - Each game starts with the token '<|START|>'
                                        - Each game ends with a token '<|FINAL_SCORE_X|>' where X is the final score
                                        - Deck status is represented as '<|DECK_X|>' where X is the number of cards remaining
                                        - P1's (your partner's) initial hand is listed after the '<|HAND_P1|>' token 
                                        - All game moves begin after the '<|START_MOVES|>' token
                                        - Game actions are encoded in specific integer ranges:
                                            * Cards are encoded as integers 11-35
                                            * Your moves (P0) are encoded as integers 36-720
                                            * Your partner's moves (P1) are encoded as integers 721-1405
                                            * Your partner's card draws are encoded as integers 1406+
                                    Your task is to analyze sequences of encoded Hanabi game moves and predict P1's next action based on the pattern of play. The input will contain multiple games for context, with each game separated by a newline. The final sequence represents the current game for which you need to make a prediction.
                                    Your response should be a single integer in the range 721-1405 corresponding to P1's most likely next move. Do not provide explanations or additional text, only output the predicted integer."""
                },     
                { "role":"user",      "content": user_json_block },
                { "role":"assistant", "content": str(sample.prediction_label) }
            ]
        }
    
    def generate_samples(self, mode="baseline") -> list[dict]:
        assert mode in {"baseline", "poe"}
        rng = random.Random(42)

        all_samples = []
        # bots = ['SimpleBot', 'ValueBot', 'HolmesBot', 'SmartBot',
        #         'InfoBot', 'AdaptBot', 'MetaBot']
        bots = ['PileBot', 'SignalBot']

        for b1 in bots:
            for b2 in bots:
                part = self._process_bot_pair(
                        b1, b2, num_samples=4500, mode=mode, rng=rng)
                for s in part:
                    fmt = (self._format_sample_poe(s) if mode=="poe"
                        else self._format_sample(s))
                    all_samples.append(fmt)
        return all_samples


def main():
    # generator = TrainingSampleGenerator(
    #     full_games_dir="training_full_games",
    #     prediction_games_dir="training_prediction_games"
    # )
    generator = TrainingSampleGenerator(
        full_games_dir="validation_full_games",
        prediction_games_dir="validation_prediction_games"
    )

    try:
        #samples = generator.generate_samples()
        samples = generator.generate_samples(mode="poe")

        if not samples:
            print("No training samples generated.")
            return

        # with open('training_samples_encoded.json', 'w', encoding='utf-8') as f:
        #     json.dump(samples, f, indent=2, ensure_ascii=False)
        with open('validation_samples_poe.json', 'w', encoding='utf-8') as f:
            json.dump(samples, f, indent=2, ensure_ascii=False)

        print(f"Generated {len(samples)} training samples.")

    except Exception as e:
        print(f"Error during sample generation: {str(e)}")

if __name__ == "__main__":
    main()
