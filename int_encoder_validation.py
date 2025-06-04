import json, re, collections

ID_RANGE = range(721, 1406)

dup_partial = set()
type_counter = collections.Counter()

with open("validation_samples_poe.json") as f:
    data = json.load(f)

for idx, sample in enumerate(data, 1):
    msgs = sample["messages"]
    assert len(msgs) == 3 and msgs[0]["role"]=="system"

    user = msgs[1]["content"]
    assistant = msgs[2]["content"].strip()

    # ---- fields present
    views  = [re.search(rf'fullgames_view_{i}":\s*"(.*?)"', user).group(1)
              for i in range(1,5)]
    partial = re.search(r'"partial_game":\s*"(.*?)"', user).group(1)

    # ---- duplicates inside sample
    flat = []
    for v in views:
        games = v.split(",<|START|>")  # naive split
        assert len(set(games)) == len(games)
        flat.extend(games)
    assert len(set(flat)) == len(flat)

    # ---- label range
    label = int(assistant)
    assert label in ID_RANGE

    # ---- class balance
    if   721 <= label < 970: type_counter["PLAY"] += 1
    elif 970<= label < 1095:         type_counter["DISCARD"]    += 1
    else:                      type_counter["HINT"]    += 1

    # ---- duplicate partial?
    assert partial not in dup_partial
    dup_partial.add(partial)

print("OK.  label distribution:", type_counter)
