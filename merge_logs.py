print("This script will merge files [log1.txt, log2.txt ... , log7.txt]")
input("Press Enter to continue...")

log_tuples = []

for i in range(1,8):
    filename = 'log'+ str(i) + '.txt'
    with open(filename) as f:
        for line in f.readlines():
            line = line.strip()
            log_tuples.append(tuple(line.split(',')))


sorted_tuples = sorted(log_tuples,key=lambda tup: int(tup[4]))

with open ('logs.txt', 'a') as logs:
    for tup in sorted_tuples:
        l = ','.join(str(s) for s in list(tup))
        logs.write(l + '\n')

print("Done writing merged log file at ./logs.txt")

with open ('visualization/logs/logs.csv', 'a') as logs:
    header = ('Client','Request','Key','Timestamp','Action','Value');
    l = ','.join(str(s) for s in list(header))
    logs.write(l + '\n')
    for tup in sorted_tuples:
        l = list(tup)
        l.pop(0)
        stripped_tup = tuple(l)
        l = ','.join(str(s) for s in list(stripped_tup))
        logs.write(l + '\n')

print("Done writing merged log file at .visualization/logs/logs.csv")
