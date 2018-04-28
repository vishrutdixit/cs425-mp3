#include <iostream>
#include <list>
#include <queue>
#include <thread>
#include <mutex>
#include <chrono>
#include <future>

#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/event.h>
#include <math.h>

#include "server.h"
#include "client.h"
#include "common.h"
#include "color.h"

#define successor finger[0].node

using namespace std;

struct connection {   // Declare connection struct type
    std::string ip;
    std::string port;
    Client* client;
    int server_fd;
    int timestamp;
};

struct fd_information {
    int pid;
    bool server_owned;
    int event_idx;
};

struct message {
    std::string text;
    int pid;
    int* V;
    int id;
    int value;
    __int64_t time;
};

std::unordered_map<unsigned int, struct connection> processes;
std::unordered_map<unsigned int, struct fd_information> fd_info;
std::unordered_map<int, int>* decisions;

std::queue<struct message> hold_back_queue;

unsigned int process_id;
static Server* s;
static bool end_session = false;
static int kq;
static int event_idx = 0;
static struct kevent* chlist;
static struct kevent* evlist;
static const struct timespec tmout = { 0, 0 };  /* return after 100ms */
static int max_delay, min_delay = 0;
static bool is_causally_ordered = false;

static int counter = 0;
static int message_counter = 0;
static bool sequencer = false;

static bool client = false;
static bool has_reply = false;
static int reply;
int predecessor = -1;

__int64_t ms_start = std::chrono::duration_cast< std::chrono::milliseconds >(std::chrono::system_clock::now().time_since_epoch()).count();

/**
 * Initializes the unordered_map holding the information needed to connect to each process.
 */
void parse_config(){
	std::ifstream config_file("multicast.config");
	std::string line;
    bool first_line = true;
	while (std::getline(config_file, line)){
        std::istringstream iss(line);
        if(first_line){
            first_line = false;
            // Get the delay arguments from the first line
    	    if (!(iss >> min_delay >> max_delay)) { // error
    			std::cout << "Error parsing config file!" << std::endl;
    			break;
    		}
        }
        else{
    	    unsigned int pid;
    		std::string ip, port;
            // Get three arguments from each line
    	    if (!(iss >> pid >> ip >> port)) { // error
    			std::cout << "Error parsing config file!" << std::endl;
    			break;
    		}

    		// Create connection struct instance for hashmap
    		struct connection connection_info;
    		connection_info.ip = ip;
    		connection_info.port = port;
    		connection_info.client = NULL;
    		connection_info.server_fd = -1;
    		connection_info.timestamp = 0;

    		// Create pair for insertion
    		std::pair<unsigned int, struct connection> entry(pid, connection_info);
    		//Insert
    		processes.insert(entry);
        }
	}
}

/**
 * Helper to print the current changelist we're watching through kqueue
 */
void print_kevents(){
    std::cout << event_idx << " fds: ";
    for(int i = 0; i < event_idx; i++){
        std::cout << chlist[i].ident << " ";
    }
    std::cout << std::endl;
}

/**
 * Removes kevent listener for file descriptor
 */
void remove_fd_from_kqueue(int fd){
    int idx = fd_info[fd].event_idx;
    fd_info.erase(fd);

    event_idx -= 1;
    unsigned int temp_fd = chlist[event_idx].ident;
    if(temp_fd != fd){ // We want to swap current event for last event, unless the current event is last
        // Deleting last event
        EV_SET(&chlist[event_idx], temp_fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
        // Reenable last event's fd in event we just deleted
        EV_SET(&chlist[idx], temp_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, 0);
    }
    // else event we want to remove is at end
    // -> do nothing, we already decremented the event_idx to exclude it from kevent call
}

/**
 * Adds kevent listener for file descriptor
 * If pid of process is unknown, a pid of -1 should be used to indicate that
 */
void add_fd_to_kqueue(int fd, int pid, bool server_owned){
    // Create entry in fd_info for passed fd
    if(fd == -1) return;
    fd_information f_info;
    f_info.pid = pid;
    f_info.server_owned = server_owned;
    f_info.event_idx = event_idx;

    std::pair<unsigned int, struct fd_information> entry(fd, f_info);
    fd_info.insert(entry);

    /* Initialize kevent structure. */
    EV_SET(&chlist[event_idx++], fd, EVFILT_READ, EV_ADD, 0, 0, 0);
}

/**
 * Prepends an message size integer's bytes to a char array, as well as
 * a char indicating the message protocol
 */
char* create_message(const char* message, int len, char protocol){
    char* output = new char[len + sizeof(int) + sizeof(char)];
    memcpy(output, &len, sizeof(int));
    memcpy(output+sizeof(int), &protocol, sizeof(char));
    memcpy(output+sizeof(int)+sizeof(char), message, len);
    return output;
}

/**
 * Thread function for sending delayed unicast message. Sends particular protocol header and
 * increments necessary counter.
 */
void delayed_usend(const char* cstr_message, int len, int fd)
{
    //Format message
    char* formatted_message = create_message(cstr_message, len, 'u');
    int delay = rand()%(max_delay - min_delay) + min_delay;
    //Sleep if not sending to self
    if(fd_info[fd].pid != process_id) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    //Write message to socket
    write_all_to_socket(fd, formatted_message, len + sizeof(char) + sizeof(int));
    delete[] formatted_message;
}

/**
 * Thread function for sending delayed multicast message. Sends particular protocol header and
 * increments necessary counter.
 */
void delayed_msend(const char* cstr_message, int len, int fd)
{
    char* formatted_message;
    if(is_causally_ordered){
        // Write the vector timestamp
        char* output = new char[len + sizeof(int)*processes.size()];
        for(int i = 0; i < processes.size(); i++){
            int timestamp = processes[i].timestamp;
            memcpy(output+(i)*sizeof(int), &timestamp, sizeof(int));
        }
        // Write the message
        memcpy(output + processes.size()*sizeof(int), cstr_message, len);
        len = processes.size()*sizeof(int) + len;
        formatted_message = create_message(output, len, 'm');
        delete[] output;
    }
    else {
        // Write the message id
        char* output = new char[len + sizeof(int)];
        int id = message_counter;

        memcpy(output, &id, sizeof(int));
        memcpy(output + sizeof(int), (void*) cstr_message, len);
        // Write the message
        len = sizeof(int) + len;
        formatted_message = create_message(output, len, 'm');
        delete[] output;
    }
    //Format message
    int delay = rand()%(max_delay - min_delay + 1) + min_delay;
    //Sleep if not sending to self
    if(fd_info[fd].pid != process_id) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    //Write message to socket
    len = len + sizeof(int) + sizeof(char);
    write_all_to_socket(fd, formatted_message, len);
    delete[] formatted_message;
}

void delay(int delay){
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
}

/**
 * Returns a string representing the current time in hrs:min:sec:ms
 */
std::string get_time(){
    __int64_t ms_past_epoch = std::chrono::duration_cast< std::chrono::milliseconds >(std::chrono::system_clock::now().time_since_epoch()).count();
    __int64_t seconds_past_epoch = time(0);
    int ms = ms_past_epoch - seconds_past_epoch*1000;
    time_t theTime = time(NULL);
    struct tm *aTime = localtime(&theTime);
    int hour=aTime->tm_hour;
    int min=aTime->tm_min;
    int sec=aTime->tm_sec;

    std::ostringstream oss;
    oss << hour <<":"<< (min<10 ? "0" : "") << min <<":"<< (sec<10 ? "0" : "")  << sec <<":"<< (ms<10 ? "0" : "") << ms;
    return oss.str();
}

/**
 * Returns the number of ms since the beginning of execution
 */
__int64_t get_ms(){
    __int64_t ms_past_epoch = std::chrono::duration_cast< std::chrono::milliseconds >(std::chrono::system_clock::now().time_since_epoch()).count();
    return ms_past_epoch - ms_start;
}

/**
 * Thread function for sending delayed sequencer message. Sends particular protocol header and
 * increments necessary counter.
 */
void delayed_sequencer_msend(int message_id, int fd, int pid){
    int len = sizeof(int)*3;
    char* formatted_message;

    //Write info
    char* output = new char[len];
    int decision = decisions[pid - 1][message_id];
    memcpy(output, &pid, sizeof(int));
    memcpy(output+sizeof(int), &message_id, sizeof(int));
    memcpy(output+2*sizeof(int), &decision, sizeof(int));

    formatted_message = create_message(output, len, 'o');
    delete[] output;

    int delay = rand()%(max_delay - min_delay + 1) + min_delay;
    //Sleep if not sending to self
    if(fd_info[fd].pid != process_id) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    //Write message to socket
    write_all_to_socket(fd, formatted_message, len + sizeof(char) + sizeof(int));
    delete[] formatted_message;
}

/**
 * Receives a unicast message
 */
void unicast_receive(int source, std::string message){
    std::cout << KGRN << "Received \"" << message << "\" from process " << source <<  ", system time is " << get_time() << RST << std::endl;
}

/**
 * Unicast sends a message to the group
 */
void unicast_send(int dest, const char* message, int len){
    std::unordered_map<unsigned int, struct connection>::const_iterator result = processes.find(dest);
    if(result == processes.end()){
        std::cout << "Error: could not unicast send to non-existent pid" << std::endl;
        return;
    }
    struct connection info = result->second;
    std::cout << KRED << "Sent \"" << message << "\" to process " << dest << ", system time is " << get_time() << RST << std::endl;
    // Send a message with simulated delay
    std::thread t(delayed_usend, message, len, info.server_fd);
    t.detach();
}

/**
 * Prints messages as they are delivered
 */
void delivered(int source, std::string message){
    std::cout << KYEL << "Message \"" << message << "\" from process " << source <<  " delivered. System time is " << get_time() << RST << std::endl;
}

/**
 * Receives a multicast message
 */
void multicast_receive(int source, std::string message){
    unicast_receive(source, message);
    if(is_causally_ordered){ if(source != process_id) processes[source].timestamp += 1; }
    else { counter += 1; }
}

/**
 * Sends a sequencer message to the group
 */
void sequencer_send(int message_id, int pid){
    std::pair<int, int> entry(message_id, counter);
    decisions[pid-1].insert(entry);
    for(auto x: processes){
        int dest = x.first;
        struct connection info = x.second;
        if(info.server_fd == -1) continue;
        std::cout << KCYN << "Sent sequence for message " << message_id << " to process " << dest << ", system time is " << get_time() << RST << std::endl;
        // Send a message with simulated delay
        std::thread t(delayed_sequencer_msend, message_id, info.server_fd, pid);
        t.detach();
    }
    // Increment the sequence number
    counter += 1;
}


/**
 * Multicast sends a message to the group
 */
void multicast_send(const char * message, int len){
    //Increment this process's timestamp
    processes[process_id].timestamp += 1;
    message_counter+=1;
    for(auto x: processes){
        int dest = x.first;
        struct connection info = x.second;
        if(info.server_fd == -1) continue;
        std::cout << KRED << "Sent \"" << message << "\" to process " << dest << ", system time is " << get_time() << RST << std::endl;
        // Send a message with simulated delay
        std::thread t(delayed_msend, message, len, info.server_fd);
        t.detach();
    }
}

/**
 * Sends a reply chord message
 */
void chord_reply(int dest, int reply, char protocol){
    char* buffer = new char[sizeof(int) + 1];
    buffer[0] = protocol;
    memcpy(buffer+1, &reply, sizeof(int));
    int len = 1 + sizeof(int);
    // Send reply message
    unicast_send(dest, buffer, len);
}

/**
 * Received a chord message and processes it based on protocol
 */
void chord_receive(int source, char* message, int len){
    if(len == 1){ // Received predecessor request
        std::cout << len << " " << message[0] << std::endl;
        if(message[0] == 'p'){
            std::cout << len << " " << message[0] << std::endl;
            std::cout << "Got predecessor request from process " << source << std::endl;
            chord_reply(source, predecessor, 'p');
        }
    }
    else { // Received predecessor reply
        std::cout << len << " " << message[0] << (message + 1) << std::endl;
        if(message[0] == 'p'){
            std::cout << "Got predecessor reply: " << *((int *)(message + 1)) << " from process " << source << std::endl;
            reply = *((int *) (message + 1));
            has_reply = true;
        }
    }
}

/**
 * Processes any updated file descriptors
 */
void process_fds(){
    int nev = kevent(kq, chlist, event_idx, evlist, event_idx, &tmout);
    if (nev == -1) {
       perror("kevent()");
       exit(1);
    }
    for(int i = 0; i < nev; i ++){ // For each event triggered
        int fd = evlist[i].ident;   // Get the file descriptor
        if (evlist[i].flags & EV_ERROR) { // report errors if any
           fprintf(stderr, "EV_ERROR: %s\n%d\n", strerror(evlist[i].data), fd);
           exit(1);
        };
        if(evlist[i].flags & EV_EOF){ // Handle disconnects
            // std::cout << "Socket fd " << fd << " for process " << fd_info[fd].pid << " disconnected." << std::endl;
            if(!fd_info[fd].server_owned){ processes[fd_info[fd].pid].client->close(); } // Close fd and set connected false
            else { ::close(fd); } // Otherwise close fd and wait for new client to connect
            remove_fd_from_kqueue(fd);
            return;
        }
        if(fd_info[fd].server_owned){ // Then it's a remote client telling us their ID so we can connect to them
            // Read the process id sent from the client
            unsigned int pid = 0;
            int read_bytes = read_all_from_socket(fd, (char *) &pid, sizeof(int));
            if(read_bytes == -1){ // Error case
                std::cout << "Error reading from socket" <<read_bytes<<std::endl;
                exit(1);
        	}
            // std::cout << "Got id: " << pid << " from client with fd: " << fd << std::endl;

            // Update process info with server file descriptor
            struct connection& info = processes[pid];
            info.server_fd = fd;
            fd_info[fd].pid = pid;

            if(pid != process_id && !processes[pid].client->is_connected()){ // If we don't already have a client for pid
                // std::cout << "Attempting to connect back to server " << pid << " at port " << info.port << std::endl;
                int client_fd = info.client->connect_to_server(info.ip, info.port);
                if(client_fd >= 0){ // Success
                    add_fd_to_kqueue(client_fd, pid, false);
                }
            }
        }
        else { //Then there is an update from another server.
            // Get the num of bytes to read
            int read_len;
            read_all_from_socket(fd, (char *) &read_len, sizeof(int));
            // Get the protocol (msend or usend)
            char protocol;
            read_all_from_socket(fd, &protocol, sizeof(char));
            if(protocol == 'm'){
                if(is_causally_ordered){
                    struct message m;
                    m.pid = fd_info[fd].pid;
                    m.V = new int[processes.size()];
                    char* read_bytes = new char[read_len+1];
                    read_all_from_socket(fd, read_bytes, read_len);
                    read_bytes[read_len] = 0;
                    int timestamp;

                    for(int i = 0; i < processes.size(); i ++){
                        memcpy(&timestamp, read_bytes+i*sizeof(int), sizeof(int));
                        m.V[i] = timestamp;
                    }

                    m.text = std::string(read_bytes+sizeof(int)*processes.size());
                    delivered(m.pid, m.text);
                    free(read_bytes);
                    hold_back_queue.push(m);
                }
                else {
                    struct message m;
                    m.pid = fd_info[fd].pid;
                    char* read_bytes = new char[read_len];
                    read_all_from_socket(fd, read_bytes, read_len);

                    int id;
                    memcpy(&id, read_bytes, sizeof(int));
                    m.id = id;

                    m.text = std::string(read_bytes+sizeof(int));
                    delivered(m.pid, m.text);
                    if(sequencer) sequencer_send(m.id, m.pid);
                    else hold_back_queue.push(m);
                }
            }
            else if(protocol == 'u'){
                // Read the message
                char* read_bytes = new char[read_len+1];
                read_all_from_socket(fd, read_bytes, read_len);
                read_bytes[read_len] = 0;
                unicast_receive(fd_info[fd].pid, std::string(read_bytes));
                chord_receive(fd_info[fd].pid, read_bytes, read_len);
            }
            else {
                int pid, id, decision;
                char buf[read_len];
                read_all_from_socket(fd, buf, read_len);
                memcpy(&pid, buf, sizeof(int));
                memcpy(&id, buf+sizeof(int), sizeof(int));
                memcpy(&decision, buf+2*sizeof(int), sizeof(int));
                // std::cout << "Decision: " << decision << " for message " << id << std::endl;
        		std::pair<int, int> entry(id, decision);
                decisions[pid-1].insert(entry);
            }
        }
    }
}


/**
 * Checks the hold-back queue for any messages that can be received
 */
void check_queue(){
    for(int j = 0; j < hold_back_queue.size(); j ++){
        struct message m = hold_back_queue.front();
        hold_back_queue.pop();

        if(is_causally_ordered){
            bool failed = false;
            for(int i = 0 ; i < processes.size(); i ++){
                int timestamp = m.V[i];
                int pid = i;
                if(m.pid == process_id) break;
                if(pid == m.pid && timestamp != (processes[pid].timestamp + 1) ){
                    // Guarantees V_q[i] == V_p[i] + 1
                    failed = true;
                    break;
                }
                if(pid != m.pid && timestamp > (processes[pid].timestamp) ){
                    // Guarantees V_q[i] <= V_p[i]
                    failed = true;
                    break;
                }
            }
            if(!failed){
                delete[] m.V;
                multicast_receive(m.pid, m.text);
            }
            else {
                hold_back_queue.push(m);
            }
        }
        else {
            std::unordered_map<int, int>::const_iterator result = decisions[m.pid-1].find(m.id);
            // std::cout << m.text << " " << m.pid << " " << m.id <<std::endl;
            //If there hasn't been a sequencer decision yet
            if(result == decisions[m.pid-1].end()){
                hold_back_queue.push(m);
            }
            else {
                int decision = result->second;
                if(counter == decision){
                    multicast_receive(m.pid, m.text);
                    decisions[m.pid-1].erase(m.id);
                }
                else {
                    // Wait till later
                    hold_back_queue.push(m);
                }
            }
        }

    }
}

/**
 * Frees memory and closes all file descriptors still in use
 */
void close_process(int sig){
    delete[] chlist;
    delete[] evlist;
    for(auto x: fd_info){
        int fd = x.first;
        struct fd_information info = x.second;
        if(info.server_owned){
            shutdown(fd, SHUT_RDWR);
            ::close(fd);
            remove_fd_from_kqueue(fd);
        }
        else {
            ::close(fd);
        }
    }
    s->close();
    delete s;
    for(auto x: processes){
        struct connection info = x.second;
        delete info.client;
    }
    delete[] decisions;
    end_session = true;
}

void setup_connections(){
    for(int i = 1; i < processes.size(); i++){
        pid_t child = fork();
        if(child == -1){ // err
            break;
        }
        char pid_str[3];
        sprintf(pid_str, "%d", i);
        if(child == 0){ // I am the child
            close(0); // Close stdin
            //close(1); // Close stdout
            execlp("./bin/runner.exe", "./bin/runner.exe", pid_str, NULL);
        }
        else { //I am the parent
        }
    }
}

// Chord node specific information
int n; // Chord node identifier

struct finger_info {
    int start;
    int node;
};

static int num_fingers = 8; // Number of fingers in table
static int num_ident = 1 << num_fingers; // Number of unique identifiers (keys,nodes)

struct finger_info finger[8];
static int client_id = 0;

/**
 * Thread function for sending delayed unicast message. Sends particular protocol header and
 * increments necessary counter.
 */
void delayed_usend_reply(const char* cstr_message, int len, int fd)
{
    // Send message
    delayed_usend(cstr_message, len, fd);

    // Get reply
    has_reply = false;
    while(!has_reply){
        process_fds();
    }
}

/**
 * Unicast sends a message to the group, expecting a reply
 */
void unicast_send_reply(int dest, const char* message, int len){
    std::unordered_map<unsigned int, struct connection>::const_iterator result = processes.find(dest);
    if(result == processes.end()){
        std::cout << "Error: could not unicast send to non-existent pid" << std::endl;
        return;
    }
    struct connection info = result->second;
    std::cout << KRED << "Sent \"" << message << "\" to process " << dest << ", system time is " << get_time() << RST << std::endl;
    // Send a message with simulated delay
    std::thread t(delayed_usend_reply, message, len, info.server_fd);
    t.join();
}

/**
 * A "message call" asking for a node's predecessor
 */
int get_predecessor(const int dest){
    char message[] = "p";
    int len = 1;
    unicast_send_reply(dest, message, len);
    return reply;
}

/**
* A "message call" asking for a node's successor
*/
int get_successor(const int dest){
    char message[] = "s";
    int len = 1;
    unicast_send_reply(dest, message, len);
    return reply;
}

/**
 * Check if an index is in an circular open/closed interval [(a,b)]
 */
int in_interval(int a, int b, int idx, int a_closed, int b_closed){
    if (a == b) return true; // TODO: check this condition later
    if(a_closed && b_closed){
        if(a > b){
            return !(b < idx && idx < a);
        }
        else{
            return (a <= idx && idx <= b);
        }
    }
    else if(!a_closed && b_closed){
        if(a > b){
            return !(b < idx && idx <= a);
        }
        return (a < idx && idx <= b);
    }
    else if(a_closed && !b_closed){
        if(a > b){
            return !(b <= idx && idx < a);
        }
        return (a <= idx && idx < b);
    }
    else if(!a_closed && !b_closed){
        if(a > b){
            return !(b <= idx && idx <= a);
        }
        return (a < idx && idx < b);
    }
    else {
        return false;
    }
}

/**
 *  Return closest finger preceding id
 */
bool closest_preceding_finger(int id){
   //TODO:
   //1. Check finger[i] for i = 7 to 0
   //2. If n < finger[i] < id, return finger[i]
   for(int i = 7; i >= 0; i--){
       if(n < finger[i].node < id){
           return finger[i].node;
       }
   }
   return n;
}

/**
 *  Ask node n to find id's predecessor
 */
int find_predecessor(int id){
    //TODO:
    //1. n' = n
    //2. while(n' < id < n'.succesor):
    //3.    n' = n'.closest_preceding_finger(id)
    //4. return n'
    int pred = n;

    //check in interval (n',n'.successor]
    while(!(in_interval(pred,successor,id,0,1))){
        if(pred == n){
            pred = closest_preceding_finger(id);
            //std::cout << "closest preceding finger is: " << pred << std::endl;
        }
        else{
            //send message to ask node pred to find its closest preceding finger
            char* pred_req = create_message((char*)NULL,0,'p');
            delayed_usend(pred_req, 1, pred);
            //pred = response from process pred

        }
    }
    std::cout << "returning predecessor as: " << pred << std::endl;
    return pred;
}

/**
 *  Ask node n to find id's successor
 */
int find_successor(int id){
    //TODO:
    //1. n' = find_predecessor(id)
    //2. return n'.successor;
    int pred = find_predecessor(id);
    if(pred == n){
        return successor;
    }
    else{
        //send message to find node pred's successor
        char* suc_req = create_message((char*)NULL,0,'s');
        delayed_usend(suc_req,1,pred);
        //wait for response and return
    }
    return 0;
}

/**
 * Update all nodes whose finger tables should refer to node n
 */
void update_finger_table(int s, int i){
    if(in_interval(n, finger[i].node, s, 1, 0)){
        finger[i].node = s;
        int p = predecessor;
        //TODO: refactor to send_update_finger_table(p, s, i);
        // p.update_finger_table(s,i);
    }
}

/**
 * Update all nodes whose finger tables should refer to node n
 */
void update_others(){
    for(int i = 0; i < num_fingers; i++){
        int p = find_predecessor((n - (1 << i)) % 256);
        //TODO: refactor to send_update_finger_table(p, n, i);
        // p.update_finger_table(n, i);
    }
}

/**
 * Initializes finger table for local node n
 * @param n_prime - an arbitrary node in the network
 */
void init_finger_table(const int n_prime){
    //TODO: refactor to get_find_successor(n_prime, finger[0].start);
    // finger[0].node = n_prime.find_successor(finger[0].start);
    //TODO: refactor to get_predecessor(successor);
    // predecessor = successor.predecessor;
    //TODO: refactor to update_predecessor(successor, n);
    // successor.predecessor = n;
    for(int i = 0; i < num_fingers-1; i++){
        if(in_interval(n, finger[i].node, finger[i+1].start, 1, 0)){
            finger[i+1].node = finger[i].node;
        }
        else {
            //TODO: refactor to get_find_successor(n_prime, finger[i+1].start);
            // finger[i+1].node = n_prime.find_successor(finger[i+1].start);
        }
    }
}

/**
 * Joins a new node to the Chord network
 * @param n_prime - an arbitrary node in the network
 */
void join(const int n_prime){
    // Just checks if n_prime is valid
    if(n_prime >= 0 && n_prime <= 31){
        for(int i = 0; i < num_fingers; i++){
            // Set starts to (n + 2^i) mod 2^8
            finger[i].start = (n + (1 << i)) % num_ident;
        }
        init_finger_table(n_prime);
        update_others();
    }
    else {
        predecessor = n;
        for(int i = 0; i < num_fingers; i++){
            finger[i].start = 1 << i;
            finger[i].node = n;
        }
    }
}

/**
 * Checks if there is user input to process and handles it if so
 */
void process_input(){
    std::string command, dest_string, message, keyname, value_string;
    int value; // Used for scanning integers into (e.g. unicast-send destination, kvstore-get value)

    std::string line;
    std::getline( std::cin, line );
    if(line.empty()){
        std::cin.clear();
        return;
    }

    int space1_idx = line.find(' ', 0);
    if(space1_idx == std::string::npos){ return; } // incorrect command format - must have at least one space
    int space2_idx = line.find(' ', space1_idx+1);
    command = line.substr(0, space1_idx);
    if(command.compare("msend") == 0){
        // Must be a multicast send
        message = line.substr(space1_idx + 1, std::string::npos);
        multicast_send(message.c_str(), message.size());
    }
    else if(command.compare("send") == 0){
        //Assume it's a unicast send
        dest_string = line.substr(space1_idx+1, space2_idx-space1_idx-1);
        message = line.substr(space2_idx + 1, std::string::npos);
        sscanf(dest_string.c_str(), "%d", &value);
        unicast_send(value, message.c_str(), message.size());
    }
    else if(command.compare("join") == 0){
        value_string = line.substr(space1_idx+1,  std::string::npos);
        sscanf(value_string.c_str(), "%d", &value);
        // TODO: create chord_join function
        // chord_join(value);
        get_predecessor(2);

    }
    else if(command.compare("find") == 0){
        value_string = line.substr(space1_idx+1, space2_idx-space1_idx-1);
        sscanf(value_string.c_str(), "%d", &value);
        keyname = line.substr(space2_idx + 1, std::string::npos);
        // TODO: create chord_find function
        // chord_find(value, keyname);
    }
    else if(command.compare("crash") == 0){
        value_string = line.substr(space1_idx+1,  std::string::npos);
        sscanf(value_string.c_str(), "%d", &value);
        // TODO: create chord_crash function
        // chord_crash(value);
    }
    else if(command.compare("show") == 0){
        value_string = line.substr(space1_idx+1,  std::string::npos);
        if(value_string.compare("all") == 0){
            // TODO: create chord_show_all function
            // chord_show_all();
        }
        else {
            sscanf(value_string.c_str(), "%d", &value);
            // TODO: create chord_show function
            // chord_show(value);
        }
    }
    else {
        std::cout << "Error: invalid command!" << std::endl;
    }
}

/**
 *  Runs the program
 */
int main(int argc, char **argv) {
    if(argc >= 3){
        std::cout << "Starting process with id " << argv[1] << " using " << argv[2] << " ordering." << std::endl;
        std::string protocol = argv[2];
        if(protocol.compare("causal") == 0){
            is_causally_ordered = true;
        }
        else if(protocol.compare("client") == 0){
            client = true;
        }
        else {
            is_causally_ordered = false;
        }
    }
    else {
        std::cout << "Starting process with id " << argv[1] << std::endl;
    }
    if(argc == 4 && !is_causally_ordered){
        sequencer = true;
    }
    sscanf(argv[1], "%d", &process_id);

    // Set cin to be non-blocking
    int flags = fcntl(0, F_GETFL, 0);
    fcntl(0, F_SETFL, flags | O_NONBLOCK);

    struct sigaction act;
    memset(&act, '\0', sizeof(act));
    act.sa_handler = close_process;
    if (sigaction(SIGINT, &act, NULL) < 0) {
        perror("sigaction");
        return 1;
    }

    parse_config();
    if(client){
        setup_connections();
        n = process_id;
        join(-1);
    }
    decisions = new std::unordered_map<int, int>[processes.size()];

    chlist = new struct kevent[2*processes.size()]; // 2 fds per process
    evlist = new struct kevent[2*processes.size()]; // 2 fds per process

    if ((kq = kqueue()) == -1) {
       perror("kqueue");
       exit(1);
    }

    s = new Server(processes[process_id].port, processes.size());
    // Set up kevent to fire events for new connections on server's listening socket
    struct kevent server_event, server_result;
    EV_SET(&server_event, s->get_socket_fd(), EVFILT_READ, EV_ADD, 0, 0, 0);

    for(auto x: processes){
        unsigned int pid = x.first;
        processes[pid].client = new Client(processes[pid].ip, processes[pid].port, process_id);
        if(processes[pid].client->is_connected()){ // If client connection attempt was successful
            add_fd_to_kqueue(processes[pid].client->get_socket_fd(), pid, false);
        }
    }

    while(!end_session){
        EV_SET(&server_event, s->get_socket_fd(), EVFILT_READ, EV_ENABLE, 0, 0, 0);
        int nev = kevent(kq, &server_event, 1, &server_result, 1, &tmout); // Check for new connections to server
        EV_SET(&server_event, s->get_socket_fd(), EVFILT_READ, EV_DISABLE, 0, 0, 0);
        if(nev == 1){
            for(int i = 0; i < server_result.data; i++){
                int fd = s->accept_client();
                if(fd >= 0){
                    add_fd_to_kqueue(fd, -1, true);
                }
            }
        }
        process_fds();
        if(!sequencer){
            if(client) process_input();
            if(!hold_back_queue.empty()){
                check_queue();
            }
        }
    }
}
