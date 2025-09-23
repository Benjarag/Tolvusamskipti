#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <random>
#include <map>

using namespace std;

// ===== DATA STRUCTURES =====
struct PortInfo {
    int port;
    string type; // "secret", "evil", "checksum", "oracle"
    string response;
};

struct PuzzleData {
    uint8_t group_id;
    uint32_t signature;
    int hidden_port;
    map<string, string> secret_phrases; // port_type -> phrase
};

// secret, evil bit, checksum, knock i einhverri roð?



// Function declarations
bool solve_secret_port(const string& ip, int port, uint8_t& group_id, uint32_t& signature, int& hidden_port);
string send_and_receive(int sock, const sockaddr_in& addr, const string& message, int timeout_sec = 2);
int extract_port_from_response(const string& response);
string identify_port_with_retry(const string& ip, int port, int max_retries = 3);

// ===== MAIN CONTROLLER =====
// class PuzzleSolver {
// private:
//     string server_ip;
//     map<string, PortInfo> ports;
//     PuzzleData data;
    
// public:
//     PuzzleSolver(const string& ip, const vector<int>& port_list) : server_ip(ip) {
    
//     }


int main(int argc, char* argv[]) {
    // function to call
    if (argc != 6) {
        cerr << "Usage: " << argv[0] << " <IP address> <port1> <port2> <port3> <port4>" << endl;
        return 1;
    }

    string ip = argv[1]; // second argument is the IP address
    vector<int> ports;
    for (int i = 2; i < 6; i++) {
        ports.push_back(atoi(argv[i])); // convert to integer and store in ports vector
    }

    // Identify which port is which
    int secret_port, evil_port, checksum_port, knocking_port = -1;
    for (int port : ports) {
        string response = identify_port_with_retry(ip, port, 3);

        if (response.find("Greetings from S.E.C.R.E.T.") != string::npos) {
            secret_port = port;
            cout << "Found S.E.C.R.E.T. port: " << port << endl;
        }
        else if (response.find("The dark side") != string::npos) {
            evil_port = port;
            cout << "Found Evil port: " << port << endl;
        }
        else if (response.find("Send me a 4-byte message") != string::npos) {
            checksum_port = port;
            cout << "Found Checksum port: " << port << endl;
        }
        else if (response.find("Greetings! I am E.X.P.S.T.N") != string::npos) {
            knocking_port = port;
            cout << "Found E.X.P.S.T.N + knocking port: " << port << endl;
        }
        else {
            cout << "Unknown port type at port: " << port << endl;
        }
    }

    if (secret_port == -1) {
        cerr << "Could not find S.E.C.R.E.T. port." << endl;
        return 2;
    }
    else if (evil_port == -1) {
        cerr << "Could not find Evil port." << endl;
        return 3;
    }
    else if (checksum_port == -1) {
        cerr << "Could not find Checksum port." << endl;
        return 4;
    }
    else if (knocking_port == -1) {
        cerr << "Could not find E.X.P.S.T.N + knocking port." << endl;
        return 5;
    }

    cout << "\n=== Port Identification Complete ===" << endl;
    cout << "Secret Port: " << secret_port << endl;
    cout << "Evil Port: " << evil_port << endl;
    cout << "Checksum Port: " << checksum_port << endl;
    cout << "Knocking Port: " << knocking_port << endl;

    // Setting up variables to hold puzzle data
    uint8_t group_id;    // not set yet
    uint32_t signature;  // not set yet
    int hidden_port;     // not set yet
    
    // Now solve the S.E.C.R.E.T. port first
    cout << "\n=== Solving Secret Port ===" << endl;
    if (solve_secret_port(ip, secret_port, group_id, signature, hidden_port)) {
        // Now group_id, signature, and hidden_port are set!
        cout << "SUCCESS: Secret port solved!" << endl;
        cout << "Group ID: " << (int)group_id << endl;
        cout << "Signature: " << signature << endl;
        cout << "Hidden Port: " << hidden_port << endl;
    } else {
        cerr << "FAILED: Could not solve secret port" << endl;
        return 1;
    }

    // TODO: Add calls to solve other ports here
    // solve_evil_port(ip, evil_port, signature);
    // solve_checksum_port(ip, checksum_port, signature);
    // knocking_port(ip, knocking_port, group_id, signature);

    return 0;


}

// secret function
// Function to solve the S.E.C.R.E.T. port puzzle (implementation to be added)
bool solve_secret_port(const string& ip, int port, uint8_t& group_id, uint32_t& signature, int& hidden_port) {
    cout << "[DEBUG] solve_secret_port called for IP: " << ip << ", port: " << port << endl;
    // 1. Generate a 32 bit secret number (and remember it for later)
    // first gennerate a random 32 bit number
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<uint32_t> dis(1, 0xFFFFFFFF);
    uint32_t secret_number = dis(gen); // here is our secret number generated
    cout << "[DEBUG] Generated secret number: " << secret_number << endl;

    int sock = socket(AF_INET, SOCK_DGRAM, 0); // Create UDP socket
    if (sock < 0) {
        perror("socket creation failed");
        return false;
    }

    // setting the timeout
    timeval tv{};
    tv.tv_sec = 2; // 2 seconds timeout
    tv.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt failed");
        close(sock);
        return false;
    }

    // Here we set up the server address structure
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);

    // 2. Send me a message where the first byte is the letter 'S' followed by 4 bytes containing your secret number (in network byte order),
    // and the rest of the message is a comma-separated list of the RU usernames of all your group members.
    string usernames = "benjaminr23,sindrib23,oliver23";

    bool step1_success = false;
    for (int attempt = 0; attempt < 3 && !step1_success; attempt++) {
        char message[1 + 4 + usernames.size()];
        message[0] = 'S'; // first byte is 'S'
        uint32_t net_secret = htonl(secret_number); // convert to network byte order
        memcpy(message + 1, &net_secret, 4); // putting the secret number in next 4 bytes as network byte order
        strcpy(message + 1 + 4, usernames.c_str()); // copy usernames after that

        int message_len = 1 + 4 + usernames.size();

        cout << "Sending initial message (attempt " << (attempt + 1) << ")..." << endl;
        if (sendto(sock, message, message_len, 0, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Send failed");
            continue; // try again
        }

        // 3. I will reply with a 5-byte message, where the first byte is your group ID and the remaining 4 bytes are a 32 bit challenge number (in network byte order)
        char challenge_response_message[5]; // the 5 byte response message
        sockaddr_in from_addr{};
        socklen_t from_len = sizeof(from_addr); 

        // Set timeout for receiving challenge
        timeval tv{};
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));


        int received = recvfrom(sock, challenge_response_message, 5, 0, (sockaddr*)&from_addr, &from_len);

        if (received == 5) {
            group_id = challenge_response_message[0];
            uint32_t challenge;
            memcpy(&challenge, challenge_response_message + 1, 4);
            challenge = ntohl(challenge); // convert from network byte order to host byte order
        
            cout << "[DEBUG] Received group ID: " << (int)group_id << ", challenge: " << challenge << endl;
            
            // 4. Combine this challenge using the XOR operation with the secret number you generated in step 1 to obtain a 4 byte signature.
            signature = challenge ^ secret_number;
            cout << "[DEBUG] Computed signature: " << signature << endl;
            step1_success = true; // we succeeded in step 1
        }
        else {
            usleep(100000); // wait 100ms before retrying
            cout << "Did not receive valid challenge response, retrying..." << endl;
        }
    }

    if (!step1_success) {
        cerr << "Failed to complete step 1 after 3 attempts." << endl;
        close(sock);
        return false;
    }

    // 5. Reply with a 5-byte message: the first byte is your group number, followed by the 4-byte signature (in network byte order).
    bool step2_success = false;
    for (int attempt = 0; attempt < 3 && !step2_success; attempt++) {
        char reply_message[5];
        reply_message[0] = group_id;
        uint32_t net_signature = htonl(signature); // convert signature to network byte order
        memcpy(reply_message + 1, &net_signature, 4); // copy signature into message

        // send the reply message
        cout << "Sending signature reply (attempt " << (attempt + 1) << ")..." << endl;
        if (sendto(sock, reply_message, 5, 0, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Send failed");
            continue; // try again
        }

        // 6. If your signature is correct, I will respond with a secret port number. Good luck!
        char port_response[1024];
        sockaddr_in from_addr{};
        socklen_t from_len = sizeof(from_addr);
        
        // Set timeout for receiving port
        timeval tv{};
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        int received = recvfrom(sock, port_response, sizeof(port_response) - 1, 0, (sockaddr*)&from_addr, &from_len);
        
        if (received > 0) {
            port_response[received] = '\0'; // null-terminate the response
            string response_str(port_response);
            cout << "[DEBUG] Received port response: " << response_str << endl;
            
            hidden_port = extract_port_from_response(response_str);
            
            if (hidden_port > 0) {
                cout << "Extracted hidden port: " << hidden_port << endl;
                step2_success = true; // we succeeded in step 2
                break; // exit the retry loop
            }
            else {
                cerr << "Could not extract hidden port from response, retrying" << endl;
                usleep(100000); // wait 100ms before retrying
            }
        }
        else {
            usleep(100000); // wait 100ms before retrying
            cout << "Did not receive valid port response, retrying..." << endl;
        }
    }
    close(sock);
    return step2_success;
    // 7. Remember to keep your group ID and signature for later, you will need them for
    // this was done with the reference variables passed in
}

int extract_port_from_response(const string& response) {
    // Simple parsing to find the port number in the response string
    for (size_t i = 0; i < response.size(); i++) {
        if (isdigit(response[i])) {
            size_t j = i;
            while (j < response.length() && isdigit(response[j])) {
                j++;
            }
            string number_str = response.substr(i, j - i);
            int port = stoi(number_str);
            
            // Basic validation
            if (port > 1024 && port < 65536) {
                return port;
            }
        }
    }
    return -1; // could not find port
}

string send_and_receive(int sock, const sockaddr_in& addr, const string& message, int timeout_sec) {
    timeval tv{};
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sendto(sock, message.c_str(), message.length(), 0, (const sockaddr*)&addr, sizeof(addr));

    char buffer[1024];
    sockaddr_in from_addr{};
    socklen_t from_len = sizeof(from_addr);
    
    int received = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&from_addr, &from_len);
    
    if (received > 0) {
        buffer[received] = '\0';
        return string(buffer);
    }
    return "";
}

string identify_port_with_retry(const string& ip, int port, int max_retries) {
    for (int attempt = 1; attempt <= max_retries; ++attempt) {
        int sock = socket(AF_INET, SOCK_DGRAM, 0); // Create UDP socket
        if (sock < 0) {
            perror("socket creation failed");
            continue;
        }

        // Setting up the sockaddr_in structure for the server
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        // Set shorter timeout for identification
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 500000; // 500ms
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        string response = send_and_receive(sock, addr, "Hello", 1);
        close(sock);

        if (!response.empty()) {
            return response; // Successfully received a response
        } else {
            cout << "Attempt " << attempt << " failed for port " << port << ". Retrying..." << endl;
        }
        // Wait before retry
        usleep(100000); // 100ms delay
    }
    return ""; // All attempts failed

}
