import json
import pandas as pd

# Load JSON data from the file
with open('output_2_players_remcards/output.json', 'r') as file:
    data = json.load(file)

# Convert the JSON data to a DataFrame
df = pd.DataFrame(data)

# Function to perform stratified sampling
def stratified_sample(df, num_samples):
    # Calculate the number of unique groups
    num_groups = df.groupby(['botname']).ngroups
    samples_per_group = max(1, num_samples // num_groups)
    
    # Perform stratified sampling
    stratified_df = df.groupby(['botname'], group_keys=False).apply(
        lambda x: x.sample(n=min(samples_per_group, len(x)), random_state=42, replace=False)
    )
    
    # Ensure exactly `num_samples` are returned
    if len(stratified_df) > num_samples:
        stratified_df = stratified_df.sample(n=num_samples, random_state=42, replace=False)
    elif len(stratified_df) < num_samples:
        additional_samples_needed = num_samples - len(stratified_df)
        additional_samples = df.sample(n=additional_samples_needed, random_state=42, replace=True)
        stratified_df = pd.concat([stratified_df, additional_samples])
    
    return stratified_df

# Sample 4000 rows for training and 1000 rows for validation for each answer type using stratified sampling
# df_yes_train = stratified_sample(df[df['answer'] == 'Yes'], 4000)
# df_no_train = stratified_sample(df[df['answer'] == 'No'], 4000)
#df_maybe_train = stratified_sample(df[df['answer'] == 'Maybe'], 4000)

# df_yes_val = stratified_sample(df[df['answer'] == 'Yes'], 1000)
# df_no_val = stratified_sample(df[df['answer'] == 'No'], 1000)
#df_maybe_val = stratified_sample(df[df['answer'] == 'Maybe'], 1000)

# Combine the sampled data for training and validation
# train_df = pd.concat([df_yes_train, df_no_train, df_maybe_val])
# val_df = pd.concat([df_yes_val, df_no_val, df_maybe_val])

#Sample piles
train_df = stratified_sample(df, 2400)
val_df = stratified_sample(df, 600)


# Reset the index and update the 'id' column for both training and validation sets
train_df = train_df.reset_index(drop=True)
train_df['id'] = train_df.index

val_df = val_df.reset_index(drop=True)
val_df['id'] = val_df.index

# Add context to each sample
#context = 'You are a player in the card game Hanabi. Your goal is to create a fireworks display by playing cards in sequence (1 to 5) for each color: red (r), yellow (y), green (g), blue (b), and white (w). The cards in your hand are ordered by age and they can have the following positions: oldest (O, leftmost), second oldest (SO), middle (M), second newest (SN), and newest (N, rightmost). On your turn, you can Tell (T) another player about the color or number of their cards, Play (P) a card to a pile, or Discard (X) a card. For example, if another player tells you "P1 T You O, SN cards are y", they are saying your oldest and second newest cards are yellow. If you play "You P SN (2r)", you play your second newest card (a red 2) and your newest card becomes your second newest card. If you discard "You X SO (3g)", you discard your second oldest card (a green 3) and the rest of cards to the right shift one position to the left. The newly drawn card ("You D card") always becomes the newest (N) card. Use hints and game state to deduce the answer of the following quesiton.\n'
#balanced_df['question'] = context + balanced_df['question']

# Convert the balanced DataFrames back to a list of dictionaries
train_data = train_df.to_dict(orient='records')
val_data = val_df.to_dict(orient='records')

# Write the training data to a new JSON file
with open('output_2_players_remcards/train_output.json', 'w') as json_file:
    json.dump(train_data, json_file, indent=4)

# Write the validation data to a new JSON file
with open('output_2_players_remcards/val_output.json', 'w') as json_file:
    json.dump(val_data, json_file, indent=4)

print(f"Saved training results to 'train_output.json'.")
print(f"Saved validation results to 'val_output.json'.")