import json
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

# Load JSON data from the file
with open('processed_output.json', 'r') as file:
    data = json.load(file)

# Convert the JSON data to a DataFrame
df = pd.DataFrame(data)

# Display basic statistics
print(df.describe())

# Display the first few rows
print(df.head())

# Set the aesthetic style of the plots
sns.set_style("whitegrid")

# Plot the distribution of 'question_round' using seaborn
plt.figure(figsize=(10, 6))
sns.histplot(df['cards_remaining'], bins=30, kde=True)
plt.title('Distribution of Question Round')
plt.xlabel('Question Round')
plt.ylabel('Frequency')
plt.show()

# Plot the count of different 'answer' using seaborn
plt.figure(figsize=(10, 6))
sns.countplot(data=df, x='question_position', order=df['question_position'].value_counts().index)
plt.title('Count of Different Position Values')
plt.xlabel('Position Value')
plt.ylabel('Count')
plt.show()

# Plot the count of different 'question_value' using seaborn
plt.figure(figsize=(10, 6))
sns.countplot(data=df, x='question_value', order=df['question_value'].value_counts().index)
plt.title('Count of Different Question Values')
plt.xlabel('Question Value')
plt.ylabel('Count')
plt.show()

# Plot the count of different 'answer' using seaborn
plt.figure(figsize=(10, 6))
sns.countplot(data=df, x='answer', order=df['answer'].value_counts().index)
plt.title('Count of Different Answer Values')
plt.xlabel('Answer Value')
plt.ylabel('Count')
plt.show()
