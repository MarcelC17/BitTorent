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

struct client_sp
{
    int nr_lines;
    vector<string> file_content;        
};

 struct client_data
{
    int client_id;
    int TYPE;
    int start_hash;
    int last_hash;
};
//adaug structura list de hash + lista de clienti
struct file_data{
    //seed/peer list
    int num_segments;
    vector<client_data> sp_list;
    vector<char*> segm; 
};

int count_send =  0;

void download_thread_func(int rank, int nr_new_files, void* filenames)
{
    string* files = (string*) filenames;
    char cmd_msg;
    int num_members;
    int nr_seed;
    int last_segm = 0;

    MPI_Send((void *) &nr_new_files, 1, MPI_INT, 0, 1, MPI_COMM_WORLD);

    for (int file_no = 0; file_no < nr_new_files; file_no++){
        cout<<rank<<"  FILENO  "<<file_no;
        file_data swarm;
        char filename[MAX_FILENAME];
        strcpy(filename, files[file_no].c_str());
        
        //send needed file
        MPI_Send((void *) &filename, MAX_FILENAME, MPI_CHAR, 0, 1, MPI_COMM_WORLD);

        
            //receive file swarm
        {
            //nr_members
            MPI_Recv((void *) &num_members, 1, MPI_INT, 0, 1 , MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            //total nr hash segm
            MPI_Recv((void *) &swarm.num_segments, 1, MPI_INT, 0, 1 , MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            //seed/peer list
            swarm.sp_list.resize(num_members);
            MPI_Recv(swarm.sp_list.data(), num_members * 4, MPI_INT, 0, 1 , MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            //file hash segm
            for (int segm_no = 0; segm_no < swarm.num_segments; segm_no++){
                char* segm_hash = (char *)malloc(sizeof(char)*HASH_SIZE);
                //get hash of a segment
                MPI_Recv((void *) segm_hash, HASH_SIZE, MPI_CHAR, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                swarm.segm.push_back(segm_hash);
            }

            //ord. number of swarm seed/leech
            // MPI_Recv((void *) &nr_seed, 1, MPI_INT, 0, 1 , MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            for(auto elem : swarm.sp_list){
                cout<<"ID "<< elem.client_id<<endl;
            }
            cout<<endl;
        }

        do{
        nr_seed = rand() % num_members;
        }
        while (swarm.sp_list[nr_seed].last_hash <= last_segm);

        cout<< "CLIENT: "<<rank <<"SEED From"<<nr_seed<<endl;
        cout<< "CLIENT: "<<last_segm << "SEED From"<< swarm.sp_list[nr_seed].last_hash<<endl;


        // send request to client
        int segm_no; 
        for ( segm_no = last_segm; segm_no < last_segm + 10 && segm_no < swarm.num_segments; segm_no++){
            MPI_Send((void *) &filename, MAX_FILENAME, MPI_CHAR, swarm.sp_list[nr_seed].client_id, 0, MPI_COMM_WORLD);
            //get client response
            MPI_Recv((void *) &cmd_msg, 1, MPI_CHAR,  swarm.sp_list[nr_seed].client_id, 1 , MPI_COMM_WORLD, MPI_STATUS_IGNORE); 

        }
        last_segm = segm_no;

         
        
        MPI_Send((void *) &filename, MAX_FILENAME, MPI_CHAR, 0, 2, MPI_COMM_WORLD);
        MPI_Send((void *) &last_segm, 1, MPI_INT, 0, 2, MPI_COMM_WORLD);
        // MPI_Recv((void *) &cmd_msg, 1, MPI_CHAR, 0, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        cout<<endl<<endl<<last_segm<<endl<<endl;
        if (last_segm == swarm.num_segments){
            last_segm = 0;
            //send finalize msg
            cout<<"FILE_NO "<< file_no << "NR_NEW "<<nr_new_files<<endl;

            string out_filename = "client" + to_string(rank) + "_" + filename;
            // cout<<out_filename<<endl;
            ofstream MyFile(out_filename);
            for (auto line:swarm.segm){
                if (MyFile.is_open()){
                    line[32] = '\0';
                    MyFile<< line << endl;
                }
            }

        } else {
            file_no --;
        }

        //free allocated mem
        for (auto k:swarm.segm){
               free(k);
        } 
    }
      
    //   if (file_no == nr_new_files - 1){
                cmd_msg = FINID;
                MPI_Send((void *) &cmd_msg, 1, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
                count_send++;
                cout<<"SENT  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!     "<< count_send <<endl;
            // } 
 
}

void upload_thread_func(unordered_map<string, client_sp> files, int rank)
{
    MPI_Status status;

    char cmd_msg = ACKID;
    char segment[HASH_SIZE];

    while (cmd_msg != FINID)
    {
        //Check for shutdown message
        MPI_Probe(MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);

        if (status.MPI_SOURCE == 0){
        //Shutdown thread
            MPI_Recv(&cmd_msg, 1, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &status);
            cout<<"SHUTDOWN"<<endl;
        } 
        else {
        //Receive requests
            MPI_Recv(segment, HASH_SIZE, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
            // cout << "client  "<<  status.MPI_SOURCE << endl;
            MPI_Send((void*) &cmd_msg, 1, MPI_CHAR, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
            // cout << "client  "<<  status.MPI_SOURCE << " SENT" << endl;
        }
    }
}


void tracker(int numtasks, int rank) {    

    unordered_map<string, file_data> database;
    unordered_map<string, int> peer_leech_count;
    
    int numfiles = 0;
    int last_segm = 0;

    char char_file_name[MAX_FILENAME];    
    char cmd_msg;

    MPI_Status status;

    //counter nr of clients to finish
    int fin_count = 0;

    //get data from clients
    for (int client_no =1; client_no < numtasks; client_no++){

        MPI_Recv((void *) &numfiles, 1, MPI_INT, client_no, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        

        
        for (int j=0; j < numfiles; j++){
            //get file     
            MPI_Recv((void *) char_file_name, MAX_FILENAME, MPI_CHAR, client_no, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            string str_file_name(char_file_name);
            

            file_data f_data;

            
            client_data cl_data;

            if (database.find(str_file_name) != database.end()){
                f_data = database[str_file_name];
            }
            //get number of segments
            MPI_Recv((void *) &f_data.num_segments, 1, MPI_INT, client_no, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            //set client data
            cl_data.client_id = client_no;
            cl_data.TYPE = SEED;
            cl_data.start_hash = 0;
            cl_data.last_hash = f_data.num_segments;
            f_data.sp_list.push_back(cl_data);


            for (int segm_no = 0; segm_no < f_data.num_segments; segm_no++){
                char* segm_hash = (char *)malloc(sizeof(char)*HASH_SIZE);
                //get hash of a segment
                MPI_Recv((void *) segm_hash, HASH_SIZE, MPI_CHAR, client_no, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                f_data.segm.push_back(segm_hash);
            }

            database[str_file_name] = f_data;  
            peer_leech_count[str_file_name] = 0;           
        }
    }

    //send confiramtion to clients
    char confirm_load = ACKID;//!!!!!!!!!!!!!!BROADCAST
    int nr_new_files = -1;
    int leech_nr = numtasks - 1;
    for (int client_no =1; client_no < numtasks; client_no++){
        MPI_Send((void *) &confirm_load, 1, MPI_CHAR, client_no, 1, MPI_COMM_WORLD);
        MPI_Recv((void *) &nr_new_files, 1, MPI_INT, client_no, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        if (nr_new_files == 0){
            leech_nr--;
        }
    }

    while (fin_count < leech_nr){
        MPI_Recv((void *) char_file_name, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &status);
        string str_file_name(char_file_name);
        file_data swarm = database[str_file_name];

        int size_members = swarm.sp_list.size();


        //Send swarm to client
        {
            cout<<endl<<endl<<"to : "<< status.MPI_SOURCE << endl;
            MPI_Send((void *) &size_members, 1, MPI_INT, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
            MPI_Send((void *) &swarm.num_segments, 1, MPI_INT, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
            MPI_Send(swarm.sp_list.data(), size_members*4, MPI_INT, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
            //Send hash segm
            for (int segm_no = 0; segm_no < swarm.num_segments; segm_no++){
                MPI_Send(swarm.segm[segm_no], HASH_SIZE, MPI_CHAR, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
            }

            // MPI_Send((void *) &peer_leech_count[str_file_name], 1, MPI_INT, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
            // peer_leech_count[str_file_name]++;
        }
        
        

        //Client finished downloading
        {
            MPI_Recv((void *) char_file_name, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv((void *) &last_segm, 1, MPI_INT, status.MPI_SOURCE, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            // cout << "client  " << status.MPI_SOURCE << "  " << char_file_name << " FINISHED " << last_segm << endl;
            // MPI_Send((void *) &cmd_msg, 1, MPI_CHAR, status.MPI_SOURCE, 2, MPI_COMM_WORLD);

            // cout<<endl<<endl<<last_segm<<endl<<endl;

            string finished_file_name(char_file_name);
            if (last_segm == 10){
                                                // cout<<endl<<endl<<"444"<<endl<<endl;
            client_data new_seed;
            new_seed.client_id = status.MPI_SOURCE;
            new_seed.last_hash = 10;
            new_seed.start_hash = 0;
            new_seed.TYPE = PEER;
            
            database[finished_file_name].sp_list.push_back(new_seed);
            // push_back(new_seed);
            } else if (last_segm < database[finished_file_name].num_segments && last_segm > 10){
                for (auto it = database[finished_file_name].sp_list.begin(); it != database[finished_file_name].sp_list.end(); ++it) {
                   if (it->client_id == status.MPI_SOURCE){
                    it->last_hash = last_segm;
                   }
                }
            } else if(last_segm == database[finished_file_name].num_segments){
                // cout<<endl<<endl<<"333 "<<endl<<endl;
                peer_leech_count[str_file_name]--;
                for (auto it = database[finished_file_name].sp_list.begin(); it != database[finished_file_name].sp_list.end(); ++it) {
                   if (it->client_id == status.MPI_SOURCE){
                    it->last_hash = last_segm;
                    it->TYPE = SEED;
                   }
                }
            }

            cout<<finished_file_name<<" client " << status.MPI_SOURCE <<endl;
            cout<<"last_segm "<< last_segm << "total " <<database[finished_file_name].num_segments << endl;

            for (auto it = database[finished_file_name].sp_list.begin(); it != database[finished_file_name].sp_list.end(); ++it) {
                cout<<"proc: "<<it->client_id<<endl;
                cout<<"last_segm: "<<it->last_hash<<endl;
                cout<<"type "<< it->TYPE<<endl;
            }
            cout<<endl<<endl<<endl;

        }
        
        //Receive finish message
        {
            cout<<"RECEIVED"<<endl;

            MPI_Probe(status.MPI_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            if (status.MPI_TAG == 0)
            {
                MPI_Recv((void *) &cmd_msg, 1, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
                if (cmd_msg == FINID){
                    fin_count++;
                }
            }
            cout<<fin_count<< "  ||  " <<numtasks<<endl;
        }

    }

    for (int client_no =1; client_no < numtasks; client_no++){
        cmd_msg = FINID;
        MPI_Send((void *) &cmd_msg, 1, MPI_CHAR, client_no, 0, MPI_COMM_WORLD);
    }    


    //free allocated memory
    for (auto k:database){
        
        for (auto f:k.second.segm){
            free(f);
        }
    }    
}

void peer(int numtasks, int rank) {
    void *status;
    int r;
    
   

    // Stores files of curent client
    unordered_map<string, client_sp> files;

    string file_in;

    // Open document containing client files
    ifstream MyReadFile;
    MyReadFile.open("in" + to_string(rank) + ".txt");
    

    getline (MyReadFile, file_in);
    int nr_files = stoi(file_in);
    char cmd_msg;

    MPI_Send((void *) &nr_files, 1, MPI_INT, 0, 1, MPI_COMM_WORLD);

    for (int client_no = 0; client_no < nr_files; client_no++)
    {
        getline(MyReadFile, file_in);
        
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

        for (int line_no = 0; line_no < nr_lines; line_no++)
        {

            getline(MyReadFile, file_in);
            file_content.push_back(file_in);

            char content_line[HASH_SIZE+1];
            strcpy(content_line, file_in.c_str());
            MPI_Send((void *) &content_line, HASH_SIZE, MPI_CHAR, 0, 1, MPI_COMM_WORLD);
            
        }

        data.file_content = file_content;

        
        files[file_name_str] = data;
    } 
    
    //citeste date fisiere noi
    getline (MyReadFile, file_in);

    int nr_new_files = stoi(file_in);

    string file_names[nr_new_files];
    //citeste fisierele
    for (int nr_new_file = 0; nr_new_file < nr_new_files; nr_new_file++)
    {    
        getline (MyReadFile, file_in);
        file_names[nr_new_file] = file_in;
    }
    
    MyReadFile.close();
    
    //asteapta ack de la tracker
    MPI_Recv((void *) &cmd_msg, 1, MPI_CHAR, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    if (cmd_msg == ACKID){
        std::thread download_thread(download_thread_func, rank, nr_new_files, (void*) file_names);
        std::thread upload_thread(upload_thread_func, files, rank);
        download_thread.join();                // pauses until first finishes
        upload_thread.join();               // pauses until second finishes
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
/*
VALABIL 0
1 - 80 - 90
2 - 70 - 80
3 - 60 - 70
AdaUGA 1 , 2, 3
1 0 - 10 - 0
2 80 - 90 - 1
3 70 - 80 - 1
*/