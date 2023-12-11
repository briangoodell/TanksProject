#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>
#include <openssl/md5.h>
#include "socket.h"
#include "message.h"

#include "cracker-gpu.h"
#include "tank.h"


#define MAX_PLAYERS 32



typedef struct {
  int client_socket;
  int index;
} args_t;


void guestMain(char* addr);
void hostMain(char * type);

int GameState;


/**
 * This just determines whether or not they're going to host the game, and if they're hosting, if they'd like
 * to change the type of instance they're hosting (tank/passwords/both)
*/
int main(int argc, char** argv) {
    printf("Welcome to the Title-less Tank Game!! If you would like to join an existing game, "
    "please type '<hostname>:<port>' otherwise, type 'host' to start your own!\n> ");


    GameState = 0;

    bool done = false;
    while(!done){
        done = true;

        char hostname[32];
        char type[20];
        int args = scanf("%s", hostname);
        
        // printf("Hmmm: %s", hostname);
        if(strcmp(hostname, "host") == 0){
             // NULL, "plain", or "passwords"                //"local", 
            if(getchar() != ' '){
                hostMain(NULL);
            }
            else{//scanf("%s", type)
                hostMain(type);
            }
        }
        else if(strcmp(hostname, "tank") == 0){
            tankMain(NULL);
        }
        else if(strchr(hostname, ':') != NULL){
            guestMain(hostname);
        }
        else{
            printf("Sorry, that input format wasn't recognized, try again!\n> ");
            done = false;
        }
    }


}

/**
 * If we're a player in the game, we just need to:
 * 1) Prompt for username and password
 * 2) Wait for the game to start (and get assigned opponent)
 * 3) Play while cracking in the background
 * 4) Dramatically display each user's password at the end of the whole thing (i.e. wait for message from host)
 * 
 * @input: char* addr   The address of the host. Guarenteed to have a ':' character 
*/
void guestMain(char* addr){

    char hostname[64];
    long port;
    char delim[1] = {':'};

    if(strchr(addr, ':') == addr){
        strcpy(hostname, "localhost");
        port = atoi(addr + 1);
    }
    else{
        char* temp = strtok(addr, delim);
        temp = strtok(NULL, delim);
        strcpy(hostname, addr);
        port = atoi(temp);
    }
    

    //TODO: HANDLE CONNECTION REFUSED

    // connect to host
    int host_socket_fd = socket_connect(hostname, port);
    if (host_socket_fd == -1) {
      perror("Failed to connect");
      exit(EXIT_FAILURE);
    }

    printf("You're connected!\nPlease enter a username so we know who you are: ");

    char username[MAX_USERNAME_LENGTH];
    scanf("%s", username);

    // TODO: Check for same username. Or need some unique indentifier in passwords. this is prob not where we'll solve the issue, but just putting a comment here so I don't forget

    bool valid = false;
    char password[MAX_USERNAME_LENGTH];
    
    do{
        printf("Now choose a 7 character password: "); // Can always have them force lowercase (or lowercase it in background) if an issue
        // TODO: Force alphanum
        scanf("%s", password);
        if(strlen(password) != 7)
            continue;
        valid = true;
        for(int i = 0; i < strlen(password); i++){
            if(password[i] != tolower(password[i]))
                valid = false;
                continue;
        }
    }while(!valid);

    uint8_t passwordHash[MD5_DIGEST_LENGTH];
    MD5((unsigned char*)password, strlen(password), passwordHash);
    // md5String(password, PASSWORD_LENGTH, passwordHash);
    
    send_init(host_socket_fd, username, passwordHash);

    // TODO: consider moving this all to tank.c 
    char * opponentsUsername;
    int opponent_socket_fd;
    start_packet_t* startInfo = receive_start(host_socket_fd);
    if(startInfo->playerNum == 1){
        // Start a listening socket for your opponent
        unsigned short our_port = 0;
        int server_socket_fd = server_socket_open(&our_port);
        if (server_socket_fd == -1) {
            perror("Server socket was not opened");
            exit(EXIT_FAILURE);
        }
        
        // Send the info of your new socket back to the host so they can forward it to your opponent
        char our_hostname[64];
        gethostname(our_hostname, 64); //TODO: Check if need to truncate hostname (i.e. remove .cs.grinnell.edu)
        send_start(host_socket_fd, 2, our_hostname, our_port);

        // Start listening for a connection
        if (listen(server_socket_fd, MAX_PLAYERS)) {
            perror("listen failed");
            exit(EXIT_FAILURE);
        }

        opponent_socket_fd = server_socket_accept(server_socket_fd);
        // check if client socket is not connected
        if (opponent_socket_fd == -1) {
            perror("accept failed");
            exit(EXIT_FAILURE);
        }
        // printf("Ready to play! Starting game!\n");

        // Once they connect, exchange greetings and then send the starting board
        send_greeting(opponent_socket_fd, username);
        opponentsUsername = receive_greeting(opponent_socket_fd);
        printf("You're battling %s, Get ready!", opponentsUsername);
    }
    else{
        // Connect to the provided socket
        opponent_socket_fd = socket_connect(startInfo->hostname, startInfo->port);
        if (opponent_socket_fd == -1) {
            perror("Failed to connect");
            exit(EXIT_FAILURE);
        }
        // printf("You're connected to your opponent!\n");

        // Exchange greetings and start listening for the first board state
        opponentsUsername = receive_greeting(opponent_socket_fd);
        send_greeting(opponent_socket_fd, username);

        printf("You're battling %s, Get ready!\n", opponentsUsername);
        
        sleep(.5);
    }
    fflush(stdout);

    tank_main_args_t* tankArgs = (tank_main_args_t*) malloc(sizeof(tank_main_args_t));

    tankArgs->player_num = startInfo->playerNum;
    tankArgs->partner_fd = opponent_socket_fd;

    pthread_t tankThread;
    if (pthread_create(&tankThread, NULL, &tankMain, (void*) tankArgs)) {
        perror("pthread_create failed");
        exit(2);
    }


    
    /** TODO: RECIEVE PASSWORDS FROM HOST */

    password_set_node_t* passwords;
    size_t numPasswordsTot = receive_and_update_password_list(host_socket_fd, &passwords);
    // FILE* password_file = NULL;
    // Read until we hit the end of the file
    // while(false){//!feof(password_file)) {
    //     // Make space to hold the username
    //     char username[MAX_USERNAME_LENGTH];

    //     // Make space to hold the MD5 string
    //     char md5_string[MD5_DIGEST_LENGTH * 2 + 1];

    //     // Make space to hold the MD5 bytes
    //     uint8_t password_hash[MD5_DIGEST_LENGTH];

    //     // Try to read. The space in the format string is required to eat the newline
    //     if(fscanf(password_file, "%s %s ", username, md5_string) != 2) {
    //     fprintf(stderr, "Error reading password file: malformed line\n");
    //     exit(2);
    //     }

    //     // Convert the MD5 string to MD5 bytes in our new node
    //     if(md5_string_to_bytes(md5_string, password_hash) != 0) {
    //     fprintf(stderr, "Error reading MD5\n");
    //     exit(2);
    //     }

    //     // Add the password to the password set
    //     // add_password(&passwords, username, password_hash);
    //     add_password_array(&passwords, username, password_hash);
    
    // }
    // Now run the password list cracker
    crack_password_list_num(passwords, numPasswordsTot);
    

    pthread_join(tankThread, NULL);

    // TODO: handle game over so can play again

    return;
}




























/******************************| HOST STUFF BELOW |******************************/

void* listen_connect(void* args);
void* listen_init(void* args);

typedef struct {
    int server_socket_fd;
}listen_connect_args_t;

/**
 * This struct is the root of the data structure that will hold users and hashed passwords.
 * There are a number of buckets, and we'll do this in bucket availibity order: if a bucket is full, go to the next one until empty.
 * Instead of trying to be fancy, we just used a mask and a bitwise AND to get a psuedo-random assignment that's also super fast
 */
// typedef struct password_set {
//   // Our buckets
//   struct password_set_node buckets[numBucketsAndMask + 1];

//   // Keeping track of the # of passwords in this set. // Num players will handle this for now
// //   int numPasswordsTot;
// } password_set_t;

// Each node in each bucket keeps the username, hash, and references to make deletion faster
// typedef struct password_set_node {
//   char username [MAX_USERNAME_LENGTH];
//   uint8_t hashed_password [MD5_DIGEST_LENGTH];
// } login_pair_t;

pthread_mutex_t passwordSet_lock = PTHREAD_MUTEX_INITIALIZER;
int numPlayers;
password_set_node_t passwords[numBucketsAndMask + 1];
int numPasswordsTot;


/**
 * If we're acting as the host for the game, then everyone will connect to us, 
 * and we'll then pair them off in groups to play. TODO: Be able to watch the games
 * In order to do this, we need to:
 * 1) Make a port and listen for connecting players
 * 2) Tell the game when to start and randomly pair off players
 * 3) Listen for their games to finish
 * 4) Pair winners and losers and repeat from the last step
 * 
 * It will also act as the central manager of the cracking software. So it will:
 * 1) Comglomerate the hashes of each user's password
 * 2) When the game starts (and thus no one else can join):
 *      a) Section off the search space depending on # of machines
 *      b) Send a copy of the list to each machines
 * 3) Listen for each machine sending its results 
*/
void hostMain(char * type){
    for(int i = 0; i < numBucketsAndMask + 1; i++){
        password_set_node empty;
        // empty.hashed_password = 0;
        passwords[i].hashed_password[0] = 0;
    }

    // Open up a socket for everyone in the class to connect to 
    unsigned short port = 0;
    int server_socket_fd = server_socket_open(&port);
    if (server_socket_fd == -1) {
        perror("Server socket was not opened");
        exit(EXIT_FAILURE);
    }

    printf("Game started! Listening for players on port \033[0;35m%d\033[0m\n", port);
    
    numPlayers = 0;
    numPasswordsTot = 0;


    if (listen(server_socket_fd, MAX_PLAYERS)) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    listen_connect_args_t connect_args;
    connect_args.server_socket_fd = server_socket_fd;

    pthread_t listen_connect_thread;
    if (pthread_create(&listen_connect_thread, NULL, &listen_connect, &connect_args)) {
        perror("pthread_create failed");
        exit(2);
    } // if

    char userInput = '\0';
    while(userInput != 's'){
        userInput = getchar();
        // TODO: Add a check and confimation if # players != num passwords - means someone's not done checking in 
    }

    /** TODO: Do the opponent assignments. Probably just have each thread check if it's odd or even. 
     *  Have one initate the process and then pass it off, or just do the whole thing, idk.*/

    GameState = 1;
    
    pthread_join(listen_connect_thread, NULL);
}


void * listen_connect(void* tempArgs){
    listen_connect_args_t* args = (listen_connect_args_t*) tempArgs;
    // This is inefficient, since connecting and disconnecting causes loss of a spot
    // but we're doing it for the sake of simplicity
    pthread_t listen_threads [MAX_PLAYERS];
    args_t listen_args [MAX_PLAYERS];

    while (numPlayers < MAX_PLAYERS){
        // userInput = getchar(); // TODO: ACTUALLY GET OUT OF THE LOOP
        // TODO: Add new thread for listening and keep this thread as waiting for user input
        fflush(stdout);
        // Wait for a client to connect
        int client_socket_fd = server_socket_accept(args->server_socket_fd);
        // check if client socket is not connected
        if (client_socket_fd == -1) {
            perror("accept failed");
            exit(EXIT_FAILURE);
        }
        if(GameState == 0){ // Don't let people connect while the game is being played. Idk what that would do.
            printf("Player #%d connected!\n", numPlayers + 1);

            // set index to first open index
            listen_args[numPlayers].client_socket = client_socket_fd;
            listen_args[numPlayers].index = numPlayers; // store client socket for thread
            // store client socket for thread
            // fds[numPlayers] = client_socket_fd; // store client socket
            // create a thread
            if (pthread_create(&listen_threads[numPlayers++], NULL, &listen_init, &(listen_args[numPlayers]))) {
                perror("pthread_create failed");
                exit(2);
            } // if
        }
    } // while


    return NULL;
}

start_packet_t* threadExchange[MAX_PLAYERS];

void * listen_init(void* input_args){
    args_t* args = (args_t*) input_args;
    int client_socket_fd = args->client_socket;
    int thread_index = args->index;
    char* recived_username;
    init_packet_t* info;
    uint8_t* recived_hash;

    // Read a message from the client
    info = receive_init(client_socket_fd);
    if (info == NULL){
        printf("WARNING: Recieved irregular initalization message from client %d with index %d", client_socket_fd, thread_index);
    }
    recived_hash = info->passwordHash; // store password from packet 
    recived_username = info->username; // store username from packet

    // We will assume that the password has been verified to meet the criteria on the sending end
    pthread_mutex_lock(&passwordSet_lock);
    int hash_index = (recived_hash[0] & numBucketsAndMask);
    while(passwords[hash_index].hashed_password[0] != 0){
        hash_index = (hash_index + 1) % (numBucketsAndMask + 1);
    }
    memcpy(&(passwords[hash_index].hashed_password), recived_hash, MD5_DIGEST_LENGTH);
    strcpy((passwords[hash_index].username), recived_username);
    numPasswordsTot++;
    pthread_mutex_unlock(&passwordSet_lock);

    printf("\"%s\" (#%d/%d) is signed in and ready to play!\n", recived_username, thread_index + 1, numPlayers);

    threadExchange[thread_index] = NULL;

    while(GameState == 0){sleep(1);}

    printf("STARTING!");
    fflush(stdout);
    // TODO: deadlocks if someone leaves 
    // TODO: We'll have an array that corresponds to each thread. They can update their values depending on wins and losses and then the next round will go off of that. I'm being lazy by not implementing it rn.
    // Send start to this thread's player
    if((thread_index % 2) == 0){
        if(thread_index == numPlayers - 1){ // IDK if this will work - might not have the scope
            // send_start(client_socket_fd, 1, NULL, 0); // TODO: Send start so they print this
            printf("You're the odd player out, please wait for the next round");
            
        }
        else{
            send_start(client_socket_fd, 1, NULL, 0);
            start_packet_t* info = receive_start(client_socket_fd);
            threadExchange[thread_index + 1] = info; // Communicate between clients
            // threadExchange[thread_index] = info; // I just have a feeling we'll need this later
        }
    }
    else{
        while(threadExchange[thread_index] == NULL){sleep(0.5);}
        send_start(client_socket_fd, 2, threadExchange[thread_index]->hostname, threadExchange[thread_index]->port);
    }
    send_password_list(client_socket_fd, passwords, numPasswordsTot);
    while(true){sleep(1);};

    return NULL;
}


/**
 * Here's the flow for beginning the game:
 * host                     Server                       Client 1                        Client 2
 * thread         main     connect     listen              main                           main
 *           create connect
 *                    listen for clients 
 *                                                        connect
 *                  create listen for client
 *                                                    prompt for username
 *                                                        send init
 *                                  recieve init        wait for start
 *                                  wait for main              
 *                                                                               same init process
 * 
 *             get 's' key
 *          change game state
 *                              send start if player 1
 *                                                      receive start
 *                                                       start server
 *                                                  send start w/ server info
 *                                  receieve start
 *                                  update global
 *                            player 2 listen waits for global
 *                                    send start
 *                                 send password list                     connect to client 1 server
 *                              wait for results of game     send greeting
 *                                                                                 send greeting
 *                                                     send board and play game
 *                                                   Main thread recieves passwords     same
 *                                                  Crack passwords. Send message either when done or on hit
*/

// TODO: Player two not being signed in causes a seg fault - also make the out/of accurate