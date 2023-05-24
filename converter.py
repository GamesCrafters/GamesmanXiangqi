pieceMapping = {
    "A": 0, "a": 1,
    "B": 2, "b": 3,
    "P": 4, "p": 5,
    "N": 6, "n": 7,
    "C": 8, "c": 9,
    "R": 10, "r": 11,
}

with open("./endgames", "r") as f:
    lines = f.readlines()
    for line in lines:
        if (line[0] != 'K'):
            print("Line {} does not start with K!".format(line))
            break
        counts = [0,0,0,0,0,0,0,0,0,0,0,0]
        secondPart = False
        for i in range(1, len(line) - 1):
            if line[i] == 'K':
                secondPart = True
                continue
            if secondPart:
                counts[pieceMapping[line[i].lower()]] += 1
            else:
                counts[pieceMapping[line[i]]] += 1
        if not secondPart:
            print("Line {} does not contain a second K!".format(line))
            break
        for i in counts:
            print(i, end="")
        print('_', end="")
        print("6"*counts[4], end="")
        print('_', end="")
        print("6"*counts[5])
