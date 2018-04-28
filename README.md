# cs425-mp2
C++ Key/Value store implementation. Supports linearizability and eventual consistency models.

## Directory Purposes
`src` — Holds all of the `.cpp` files that we wrote

`include` — Holds all of the `.h` files that we wrote

`build` — Holds all of the `.o` files that are used when compiling

`bin` — Holds all of the `.exe` files that are used to execute the search program

## How to Use
The following command will start up a chord network using node 0 as the client. All nodes specified in  `multicast.config` will initially connect to the client to allow for message passing; however, nodes will only be properly initialized and added to the network after a join request.

`./bin/runner.exe 0 client`

To add node p to the network:

`join <p>`

To ask node p for key k:

`find <p> <k>`

To inform node p to perform a clean crash:

`crash <p>`

To show node p's finger table:

`show <p>`

To show all finger tables of online nodes"

`show all`

## Implementation Details

### MP 1 Implementation
Network delay simulation was accomplished by spawning n-1 threads (one for each process receiving the message) and sleeping them for a random duration of time bounded by `min_delay` and `max_delay` before sending. Note that sending to yourself doesn't incur any simulated delay. For causal ordering, we implemented the vector timestamp based algorithm outlined in the textbook on page 657. This method uses a hold-back queue to avoid delivering a message until it has delivered all messages causally preceding it. To multicast a message to group g, process p adds 1 to its entry in the timestamp and multicasts the message with the timestamp to group g. To meet the causal constraints, p<sub>i</sub> will wait until it has delivered any earlier message sent by p<sub>j</sub> and it has delivered any message that p<sub>j</sub> had delivered at the time it multicast the message. Total ordering was implemented with the help of a sequencer. A process wishing to multicast a message will append a unique identifier (id) and pass the message along to the sequencer as well as members of g. The sequencer is implemented as its own process, and it is responsible for maintaining a group specific sequence number and assigning increasing and consecutive sequence numbers to messages it delivers. The sequencer announces messages by multicasting the ordered messages to group g.

### MP 2 Implementation
The key/value store was built on the existing multicast implementation from mp1. In order to achieve linearizability, the total ordering scheme was used with a sequencer to ensure that all messages are received in the same order; this implies linearizability by preserving the global order of non-overlapping operations. Each process stores its own copy of the data in a hashtable mapping the key (all chars) to a tuple containing the value and timestamp. To achieve eventual consistency, the "last writer wins" approach was used with writing processes providing timestamps to discern order. With R+W > #nodes, this guarantees that the most updated value will always be read due to the overlap between reading and writing proceesses. This implementation multicasts to W-1 or R-1 other processes as well as itself when reading or writing. The timestamps are recorded using system time from the start of the lifecycle of each process.

### MP 3 implementation
For this project, we removed our code pertaining to the key/value store and used the message passing architecture from MP1. On startup, the thread pertaining to the client connects to all other nodes in `multicast.config`; this way, the client will be the only process able to take input from the terminal. We followed the pseudocode from "Chord: A Scalable Peer-to-peer Lookup Service for Internet Applications" for implementation details. For calls asking another node to fetch some value, a message is sent to process p with a unique 1-byte protocol identifier. To ensure that messages are linearized, we implemented what we call "message-calls". In order to ensure that our current execution can continue after receiving the result of request send to another node, we spawn a thread for each "message-call" that sends the message, and the thread also replies to other messages while it wait for its message-call reply. The main thread of each process will be suspended while this occurs, but note that additional "message-calls" may be performed as a result of the waiting process replying to other messages. This does not post any problems since message-calls will form a nested dependency relation, where the most nested message-call's reply will come first, allowing us to safely retrieve information from other nodes in the network until we return to the main thread.

### Performance Evaluation

|     | Phase 1: Average # of msgs | Phase 2: Average # of msgs |
| --- | --- | --- |
|P=4,  N=?| 67.625  | 177.25 |
|P=8,  N=?| 83.875  | 124.25 |
|P=10, N=?|  120    | 150.5  |
|P=20, N=?|  200.65 | 187.75 |
|P=30, N=?|  342.4  | 225.25 |

The main bottleneck when joining nodes to the network is the update_others function. The number of messages necessary to update other nodes when a new node p is added to the network is expected to increase. We see this trend in our table above; our results seem to follow the log^2(N) rate for joins and log(N) for finds. Because log^2(N) will be larger than log(N) for big values, we see that the average messages sent in Phase 1 is more than Phase 2 for P = 30. This trend seems to extrapolate nicely with our network.


## Partner Work
We worked on this project together for the most part. Vish implemented the basic helped function for the chord network (find_predecessor, find_successor, closest_preceding_finger). Jon worked on the message layer and finger table initialization code. All other parts were done together.
