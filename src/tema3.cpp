#include <mpi.h>
#include <thread>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <vector>
#include <sstream>
#include <string.h>
#include <unordered_map>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100
/* Client types */
#define SEED 0
#define PEER 1
#define LEECHER 2

/* Command flags */
#define FINID '0'
#define ACKID '1'


using namespace std;

/* 
* @brief Files for a specific client
*/
struct client_sp
{
    int nr_lines; //< Number of hash strings
    vector<string> file_content; //< Hash strings, input order
};

/*
* @brief Client information as given by tracker(swarm client info)
*/
 struct client_data
{
    int client_id; //< Client rank
    int TYPE; //< Client role

    /* Hash positions by input file order */
    int start_hash; //< First downloaded hash string
    int last_hash; //< Last downloaded hash string
};


/*
* @brief File swarm
*/
struct file_data{
    int num_segments; //< Number of hash strings
    vector<client_data> sp_list; //< Client swarm
    vector<char*> segm; //< Hash strings, input order
};

/*
* @brief Receives and stores swarm from tracker
*/
void receive_swarm(int* num_members, file_data* swarm){

    MPI_Recv((void *) num_members, 1, MPI_INT, 0, 1 , MPI_COMM_WORLD, MPI_STATUS_IGNORE); 

    MPI_Recv((void *) &swarm->num_segments, 1, MPI_INT, 0, 1 , MPI_COMM_WORLD, MPI_STATUS_IGNORE); 
    
    swarm->sp_list.resize(*num_members); //< Set new size of seed/peer
    
    MPI_Recv(swarm->sp_list.data(), *num_members * 4, MPI_INT, 0, 1 , MPI_COMM_WORLD, MPI_STATUS_IGNORE); //< Get seed/peer list

    /* Receive requested file hash segments*/
    for (int segm_no = 0; segm_no < swarm->num_segments; segm_no++){
        char* segm_hash = (char *)malloc(sizeof(char)*HASH_SIZE);
        MPI_Recv((void *) segm_hash, HASH_SIZE, MPI_CHAR, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        swarm->segm.push_back(segm_hash);
    }
}

/*
* @brief Sends request 
*/
int request_client(int last_segm, file_data* swarm, char* filename, int requested_client, char* cmd_msg){
        int segm_no; //< Last sent segment

        for (segm_no = last_segm; segm_no < last_segm + 10 && segm_no < swarm->num_segments; segm_no++){
        //Send hash to client
        MPI_Send((void *) swarm->segm[segm_no], HASH_SIZE, MPI_CHAR, swarm->sp_list[requested_client].client_id, 0, MPI_COMM_WORLD);
        //Get response ('ACK')
        MPI_Recv((void *) cmd_msg, 1, MPI_CHAR,  swarm->sp_list[requested_client].client_id, 1 , MPI_COMM_WORLD, MPI_STATUS_IGNORE); 
   
    }
    return segm_no;
}

/*
* @brief Sends request and manages downloads
*/
void download_thread_func(int rank, int nr_new_files, void* filenames)
{
    
    string* files = (string*) filenames; //< List of requested files 

    int num_members = 0; //< Number of members in file sworm
    int requested_client = 0; //< Client to receive download request
    int last_segm = 0; //< Last downloaded segment for a specific file

    char cmd_msg = ACKID; //< Tracks command messages

    MPI_Send((void *) &nr_new_files, 1, MPI_INT, 0, 1, MPI_COMM_WORLD); //< Sends total request number

    /* Requests files in given order */
    for (int file_no = 0; file_no < nr_new_files; file_no++){
        
        file_data swarm; //< Swarm of current file
        char filename[MAX_FILENAME]; //< Name of current file
        strcpy(filename, files[file_no].c_str());
         
        MPI_Send((void *) &filename, MAX_FILENAME, MPI_CHAR, 0, 1, MPI_COMM_WORLD); //< Request swarm from tracker 
        receive_swarm(&num_members, &swarm); //<Receive swarm from tracker

        /* Pick client for next request*/
        do{
        requested_client = rand() % num_members;
        }
        while (swarm.sp_list[requested_client].last_hash <= last_segm);

                
        last_segm = request_client(last_segm, &swarm,filename, requested_client, &cmd_msg);

         
        /* Send feedback to tracker (every 10 segments) */
        MPI_Send((void *) &filename, MAX_FILENAME, MPI_CHAR, 0, 2, MPI_COMM_WORLD);
        MPI_Send((void *) &last_segm, 1, MPI_INT, 0, 2, MPI_COMM_WORLD);

        /* File download complete */
        if (last_segm == swarm.num_segments){
            last_segm = 0;
            string out_filename = "client" + to_string(rank) + "_" + filename;
            ofstream MyFile(out_filename);
            for (auto line:swarm.segm){
                if (MyFile.is_open()){
                    line[32] = '\0';
                    MyFile<< line << endl;
                }
            }

        } else {
            /* Incomplete download, keep on current file */
            file_no --;
        }

        /* Free allocated memory */
        for (auto k:swarm.segm){
               free(k);
        } 
    }
      
    cmd_msg = FINID;
    MPI_Send((void *) &cmd_msg, 1, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
 
}

/*
* @brief Accepts requests from clients and sends file segments ('ACK')
*/
void upload_thread_func(unordered_map<string, client_sp> files, int rank)
{
    MPI_Status status;

    /* Store variables */
    char cmd_msg = ACKID; //< Command message
    char segment[HASH_SIZE]; //< Hash string

    
    while (cmd_msg != FINID)
    {
        /* Track shutdown message */
        MPI_Probe(MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);

        if (status.MPI_SOURCE == 0){
            MPI_Recv(&cmd_msg, 1, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &status); //< Receive message from tracker
        } 
        else {
            /* Receive and answer requests from clients*/
            MPI_Recv(segment, HASH_SIZE, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
            MPI_Send((void*) &cmd_msg, 1, MPI_CHAR, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
        }
    }
}

/*
* @brief Get file info from clients
*/
void get_client_data(int numfiles, char* char_file_name, int client_no,  unordered_map<string, file_data> *database){
    for (int file_no=0; file_no < numfiles; file_no++){
        /* Receive fiel name */
        MPI_Recv((void *) char_file_name, MAX_FILENAME, MPI_CHAR, client_no, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        /* Store file data */
        string str_file_name(char_file_name);
        file_data f_data;
        client_data cl_data;
        
        /* Check if swarm exists*/
        if (database->find(str_file_name) != database->end()){
            f_data = (*database)[str_file_name];
        }
        
        /* Get number of hash segments*/
        MPI_Recv((void *) &f_data.num_segments, 1, MPI_INT, client_no, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        /* Set swarm client data*/
        cl_data.client_id = client_no;
        cl_data.TYPE = SEED;
        cl_data.start_hash = 0;
        cl_data.last_hash = f_data.num_segments;
        f_data.sp_list.push_back(cl_data);
        /* Get file hash segments*/
        for (int segm_no = 0; segm_no < f_data.num_segments; segm_no++){
            char* segm_hash = (char *)malloc(sizeof(char)*HASH_SIZE);
            MPI_Recv((void *) segm_hash, HASH_SIZE, MPI_CHAR, client_no, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            f_data.segm.push_back(segm_hash);
        }
        (*database)[str_file_name] = f_data;  
    }
}

/*
* @brief Sends swarm and data to client
*/
void send_swarm(int size_members, MPI_Status* status, file_data* swarm){
    MPI_Send((void *) &size_members, 1, MPI_INT, status->MPI_SOURCE, 1, MPI_COMM_WORLD);
    MPI_Send((void *) &swarm->num_segments, 1, MPI_INT, status->MPI_SOURCE, 1, MPI_COMM_WORLD);
    MPI_Send(swarm->sp_list.data(), size_members*4, MPI_INT, status->MPI_SOURCE, 1, MPI_COMM_WORLD);
    for (int segm_no = 0; segm_no < swarm->num_segments; segm_no++){
        MPI_Send(swarm->segm[segm_no], HASH_SIZE, MPI_CHAR, status->MPI_SOURCE, 1, MPI_COMM_WORLD);
    }
}

/*
* @brief Updates database based on information received from client
*/
void update_database(int last_segm, MPI_Status* status, unordered_map<string, file_data> *database, string finished_file_name){
    /* Adds new element to the swarm*/
    if (last_segm == 10){
    client_data new_seed;
    new_seed.client_id = status->MPI_SOURCE;
    new_seed.last_hash = 10;
    new_seed.start_hash = 0;
    new_seed.TYPE = PEER;
    (*database)[finished_file_name].sp_list.push_back(new_seed);
    } else if (last_segm < (*database)[finished_file_name].num_segments && last_segm > 10){
        /* Updates existing element */
        for (auto it = (*database)[finished_file_name].sp_list.begin(); it != (*database)[finished_file_name].sp_list.end(); ++it) {
           if (it->client_id == status->MPI_SOURCE){
            it->last_hash = last_segm;
           }
        }
    } else if(last_segm == (*database)[finished_file_name].num_segments){
        /* Sets element as seed if file finished downloading */
        for (auto it = (*database)[finished_file_name].sp_list.begin(); it != (*database)[finished_file_name].sp_list.end(); ++it) {
           if (it->client_id == status->MPI_SOURCE){
            it->last_hash = last_segm;
            it->TYPE = SEED;
           }
        }
    }
}

/*
* @brief Manages client database
*/
void tracker(int numtasks, int rank) {    

    unordered_map<string, file_data> database; //< Clients database
    int numfiles = 0; //< Number of files of a speciffic client
    int last_segm = 0; //< Last segment of a specific download

    /* Store variables */
    char char_file_name[MAX_FILENAME];    
    char cmd_msg;
    MPI_Status status;

    int fin_count = 0; //< Number of clients to finish download


    /* Receive data from clients */
    for (int client_no =1; client_no < numtasks; client_no++){
        MPI_Recv((void *) &numfiles, 1, MPI_INT, client_no, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        get_client_data(numfiles, char_file_name, client_no, &database);
    }

    /* Sends confirmation to clients. Computes nr of clients to request download */
    char confirm_load = ACKID;
    int nr_new_files = -1;
    int threads_to_download = numtasks - 1; //< Number of clients with file requests
    for (int client_no =1; client_no < numtasks; client_no++){
        MPI_Send((void *) &confirm_load, 1, MPI_CHAR, client_no, 1, MPI_COMM_WORLD);
        MPI_Recv((void *) &nr_new_files, 1, MPI_INT, client_no, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        if (nr_new_files == 0){
            threads_to_download--;
        }
    }

    /* Until all threads finished download */
    while (fin_count < threads_to_download){
        
        /* Receive file request from client */
        MPI_Recv((void *) char_file_name, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &status);
        string str_file_name(char_file_name);

        file_data swarm = database[str_file_name]; //< Swarm of requested file 
        int size_member = swarm.sp_list.size(); //< Swarm size

        send_swarm(size_member, &status, &swarm);
        
        

        /* Receive report from client*/
        MPI_Recv((void *) char_file_name, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv((void *) &last_segm, 1, MPI_INT, status.MPI_SOURCE, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    
        string finished_file_name(char_file_name);
    
        update_database(last_segm, &status, &database, finished_file_name);
            
        /* Receive download finished from client */
        MPI_Probe(status.MPI_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        if (status.MPI_TAG == 0)
        {
            MPI_Recv((void *) &cmd_msg, 1, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
            if (cmd_msg == FINID){
                fin_count++;
            }
        }
    }

    /* Turn off upload threads */
    for (int client_no =1; client_no < numtasks; client_no++){
        cmd_msg = FINID;
        MPI_Send((void *) &cmd_msg, 1, MPI_CHAR, client_no, 0, MPI_COMM_WORLD);
    }    

    /* Free allocated memory */
    for (auto k:database){
        
        for (auto f:k.second.segm){
            free(f);
        }
    }    
}

/*
* @brief Reads input files data
*/
void read_input_files(int nr_files, ifstream *MyReadFile, string file_in,  unordered_map<string, client_sp> *files){
    for (int client_no = 0; client_no < nr_files; client_no++)
    {
        getline(*MyReadFile, file_in);
        
        istringstream file_credentials(file_in);
        string file_name_str;
        string line_no_str;
        getline(file_credentials, file_name_str, ' ');
        getline(file_credentials, line_no_str, ' ');
        
        char file_name[MAX_FILENAME];
        strcpy(file_name, file_name_str.c_str());
        MPI_Send((void *) &file_name, MAX_FILENAME, MPI_CHAR, 0, 1, MPI_COMM_WORLD);
        int nr_lines = stoi(line_no_str);
        MPI_Send((void *) &nr_lines, 1, MPI_INT, 0, 1, MPI_COMM_WORLD);

        client_sp data;
        data.nr_lines = nr_lines;
        vector<string> file_content;
        /* Reads hash strings */
        for (int line_no = 0; line_no < nr_lines; line_no++)
        {
            getline(*MyReadFile, file_in);
            file_content.push_back(file_in);
            char content_line[HASH_SIZE+1];
            strcpy(content_line, file_in.c_str());
            MPI_Send((void *) &content_line, HASH_SIZE, MPI_CHAR, 0, 1, MPI_COMM_WORLD);
            
        }
        data.file_content = file_content;
        (*files)[file_name_str] = data;
    }
}

void peer(int numtasks, int rank) {
    void *status;
    char cmd_msg;
    
    unordered_map<string, client_sp> files; //< Stores files of current client

    string file_in; //< Read from file

    /* Open file doc and read line by line*/
    ifstream MyReadFile;
    MyReadFile.open("in" + to_string(rank) + ".txt");
    
    getline (MyReadFile, file_in);
    int nr_files = stoi(file_in); //< get number of files

    MPI_Send((void *) &nr_files, 1, MPI_INT, 0, 1, MPI_COMM_WORLD); //< Send number of files to tracker
    
    read_input_files(nr_files, &MyReadFile, file_in, &files);
    
    getline (MyReadFile, file_in); //< Get number of files to be downloaded
    int nr_new_files = stoi(file_in);

    /* Read and store new files */
    string file_names[nr_new_files]; 
    for (int nr_new_file = 0; nr_new_file < nr_new_files; nr_new_file++)
    {    
        getline (MyReadFile, file_in);
        file_names[nr_new_file] = file_in;
    }
    MyReadFile.close();
    
    /* Wait for response from tracket*/
    MPI_Recv((void *) &cmd_msg, 1, MPI_CHAR, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    /* Start threads */
    if (cmd_msg == ACKID){
        std::thread download_thread(download_thread_func, rank, nr_new_files, (void*) file_names);
        std::thread upload_thread(upload_thread_func, files, rank);
        download_thread.join();                //< Pauses until first finishes
        upload_thread.join();               //< Pauses until second finishes
    } else cout<<"Tracker didnt receive the data. Device: "<<rank;

}
 
int main (int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}