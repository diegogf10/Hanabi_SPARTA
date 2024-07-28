import json
import pandas as pd

# Load JSON data from the file
with open('output.json', 'r') as file:
    data = json.load(file)

# Convert the JSON data to a DataFrame
df = pd.DataFrame(data)

# Number of samples for each answer type
num_samples = 20000

# Function to perform stratified sampling
def stratified_sample(df, num_samples):
    # Calculate the number of unique groups
    num_groups = df.groupby(['botname', 'cards_remaining', 'question_position', 'question_value']).ngroups
    samples_per_group = max(1, num_samples // num_groups)
    
    # Perform stratified sampling
    stratified_df = df.groupby(['botname', 'cards_remaining', 'question_position', 'question_value'], group_keys=False).apply(
        lambda x: x.sample(n=samples_per_group, random_state=42, replace=True) if len(x) > samples_per_group else x
    )
    
    # If there are fewer samples than required, sample with replacement
    if len(stratified_df) < num_samples:
        additional_samples_needed = num_samples - len(stratified_df)
        additional_samples = df.sample(n=additional_samples_needed, random_state=42, replace=True)
        stratified_df = pd.concat([stratified_df, additional_samples])
    
    return stratified_df
# Sample 20k rows for each answer type using stratified sampling
df_yes_sampled = stratified_sample(df[df['answer'] == 'Yes'], num_samples)
df_no_sampled = stratified_sample(df[df['answer'] == 'No'], num_samples)
df_maybe_sampled = stratified_sample(df[df['answer'] == 'Maybe'], num_samples)

# Combine the sampled data
balanced_df = pd.concat([df_yes_sampled, df_no_sampled, df_maybe_sampled])

# Reset the index and update the 'id' column
balanced_df = balanced_df.reset_index(drop=True)
balanced_df['id'] = balanced_df.index

# Add context to each sample
context = 'You are a player in the card game Hanabi. Your goal is to create a fireworks display by playing cards in sequence (1 to 5) for each color: red (r), yellow (y), green (g), blue (b), and orange (o). The cards in your hand are ordered by age and they can have the following positions: oldest (O, leftmost), second oldest (SO), middle (M), second newest (SN), and newest (N, rightmost). On your turn, you can Tell (T) another player about the color or number of their cards, Play (P) a card to a pile, or Discard (X) a card. For example, if another player tells you "P1 T You O, SN cards are y", they are saying your oldest and second newest cards are yellow. If you play "You P SN (2r)", you play your second newest card (a red 2) and your newest card becomes your second newest card. If you discard "You X SO (3g)", you discard your second oldest card (a green 3) and the rest shift left. The newly drawn card ("You D card") always becomes the newest (N) card. Use hints to deduce your cards.\n'
balanced_df['question'] = context + balanced_df['question']


# Convert the balanced DataFrame back to a list of dictionaries
balanced_data = balanced_df.to_dict(orient='records')

# Write the balanced data to a new JSON file
with open('processed_output.json', 'w') as json_file:
    json.dump(balanced_data, json_file, indent=4)

print(f"Saved balanced results to 'processed_output.json'.")
