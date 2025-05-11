/*
 * University of Antioquia - Operating Systems Course
 * Practice #3 - Matrix Multiplication Using Processes
 * Team Members:
 * - Juan Pablo Gómez López - 1037665653
 * - Danilo Tovar - 1193578670
 * 
 * IPC Mechanism: Shared Memory
 * Reason: None. Picked randomly.
 */

 #include <iostream>
 #include <fstream>
 #include <vector>
 #include <chrono>
 #include <string>
 #include <sstream>
 #include <cstring>
 #include <unistd.h>
 #include <sys/wait.h>
 #include <sys/shm.h>
 #include <sys/stat.h>
 #include <sys/mman.h>
 #include <fcntl.h>
 #include <getopt.h>
 #include <iomanip>
 #include <limits>
 #include <filesystem>

 namespace fs = std::filesystem;

 using namespace std;

// Define a struct to store shared memory metadata
struct SharedMatrixData {
    int n_rows;
    int n_cols;
    // The actual matrix data will be stored after this struct in memory
};

// Function declarations remain unchanged
vector<vector<double>> readMatrix(const string& filename, int& rows, int& cols);
void writeMatrix(const string& filename, const vector<vector<double>>& matrix);
vector<vector<double>> multiplyMatricesSequential(const vector<vector<double>>& A, const vector<vector<double>>& B);
void* createSharedMatrix(const vector<vector<double>>& matrix, const string& shm_name);
double getMatrixElement(void* shm_ptr, int row, int col);
void setMatrixElement(void* shm_ptr, int row, int col, double value);
vector<vector<double>> extractMatrix(void* shm_ptr);
void calculateMatrixPortion(void* matrixA, void* matrixB, void* matrixC, int start_row, int end_row);
void cleanupSharedMemory(const string& shm_name, void* ptr, size_t size);
vector<vector<double>> multiplyMatricesParallel(const vector<vector<double>>& A, const vector<vector<double>>& B, int num_processes);
void printUsage(const char* programName);

// Function to read matrix from file
vector<vector<double>> readMatrix(const string& filename, int& rows, int& cols) {
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Error opening file: " << filename << endl;
        exit(1);
    }

    vector<vector<double>> matrix;
    string line;
    rows = 0;
    
    // Read the file line by line
    while (getline(file, line)) {
        if (!line.empty()) {
            istringstream iss(line);
            vector<double> row;
            double value;
            
            // Parse values from the current line
            while (iss >> value) {
                row.push_back(value);
            }
            
            // Only add non-empty rows
            if (!row.empty()) {
                // Set the number of columns based on the first row
                if (rows == 0) {
                    cols = row.size();
                } else if (row.size() != cols) {
                    cerr << "Error: Inconsistent number of columns in row " << rows << endl;
                    exit(1);
                }
                
                matrix.push_back(row);
                rows++;
            }
        }
    }
    
    if (rows == 0) {
        cerr << "Error: Empty matrix file or no valid data" << endl;
        exit(1);
    }
    
    file.close();
    return matrix;
}

// Function to write matrix to file
void writeMatrix(const string& filename, const vector<vector<double>>& matrix) {
    ofstream file(filename);
    if (!file.is_open()) {
        cerr << "Error opening file for writing: " << filename << endl;
        exit(1);
    }

    // Set precision for full double accuracy
    file << fixed << setprecision(numeric_limits<double>::max_digits10);

    int rows = matrix.size();
    int cols = matrix[0].size();

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            file << matrix[i][j];
            if (j < cols - 1) file << " ";
        }
        file << endl;
    }

    file.close();
}


// Function to multiply matrices sequentially
vector<vector<double>> multiplyMatricesSequential(const vector<vector<double>>& A, const vector<vector<double>>& B) {
    int N = A.size();       // Number of rows in A
    int M = A[0].size();    // Number of columns in A (= Number of rows in B)
    int P = B[0].size();    // Number of columns in B
    
    // Initialize result matrix C with zeros
    vector<vector<double>> C(N, vector<double>(P, 0.0));
    
    // Perform matrix multiplication
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < P; j++) {
            // Calculate C[i][j]
            for (int k = 0; k < M; k++) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }
    
    return C;
}

// Function to create shared memory for a matrix
void* createSharedMatrix(const vector<vector<double>>& matrix, const string& shm_name) {
    int rows = matrix.size();
    int cols = matrix[0].size();
    
    // Calculate total size needed for SharedMatrixData + matrix data
    size_t matrix_data_size = rows * cols * sizeof(double);
    size_t total_size = sizeof(SharedMatrixData) + matrix_data_size;
    
    // Create shared memory
    int shm_fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        cerr << "Error creating shared memory: " << strerror(errno) << endl;
        exit(1);
    }
    
    // Set the size of the shared memory segment
    if (ftruncate(shm_fd, total_size) == -1) {
        cerr << "Error setting size of shared memory: " << strerror(errno) << endl;
        exit(1);
    }
    
    // Map the shared memory segment into the address space
    void* ptr = mmap(0, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (ptr == MAP_FAILED) {
        cerr << "Error mapping shared memory: " << strerror(errno) << endl;
        exit(1);
    }
    
    // Fill in the metadata
    SharedMatrixData* metadata = static_cast<SharedMatrixData*>(ptr);
    metadata->n_rows = rows;
    metadata->n_cols = cols;
    
    // Get pointer to the matrix data area
    double* matrix_data = reinterpret_cast<double*>(static_cast<char*>(ptr) + sizeof(SharedMatrixData));
    
    // Copy matrix data to shared memory
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            matrix_data[i * cols + j] = matrix[i][j];
        }
    }
    
    return ptr;
}

// Function to access matrix data in shared memory
double getMatrixElement(void* shm_ptr, int row, int col) {
    SharedMatrixData* metadata = static_cast<SharedMatrixData*>(shm_ptr);
    double* matrix_data = reinterpret_cast<double*>(static_cast<char*>(shm_ptr) + sizeof(SharedMatrixData));
    return matrix_data[row * metadata->n_cols + col];
}

// Function to set matrix element in shared memory
void setMatrixElement(void* shm_ptr, int row, int col, double value) {
    SharedMatrixData* metadata = static_cast<SharedMatrixData*>(shm_ptr);
    double* matrix_data = reinterpret_cast<double*>(static_cast<char*>(shm_ptr) + sizeof(SharedMatrixData));
    matrix_data[row * metadata->n_cols + col] = value;
}

// Function to extract matrix from shared memory
vector<vector<double>> extractMatrix(void* shm_ptr) {
    SharedMatrixData* metadata = static_cast<SharedMatrixData*>(shm_ptr);
    int rows = metadata->n_rows;
    int cols = metadata->n_cols;
    
    // Get pointer to the matrix data area
    double* matrix_data = reinterpret_cast<double*>(static_cast<char*>(shm_ptr) + sizeof(SharedMatrixData));
    
    // Copy data to a standard C++ vector
    vector<vector<double>> matrix(rows, vector<double>(cols));
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            matrix[i][j] = matrix_data[i * cols + j];
        }
    }
    
    return matrix;
}

// Child process function to calculate portion of the result matrix
void calculateMatrixPortion(void* matrixA, void* matrixB, void* matrixC, 
                           int start_row, int end_row) {
    SharedMatrixData* metadataA = static_cast<SharedMatrixData*>(matrixA);
    SharedMatrixData* metadataB = static_cast<SharedMatrixData*>(matrixB);
    SharedMatrixData* metadataC = static_cast<SharedMatrixData*>(matrixC);
    
    int M = metadataA->n_cols;  // Columns in A = Rows in B
    int P = metadataB->n_cols;  // Columns in B
    
    // Calculate assigned portion of the result matrix
    for (int i = start_row; i < end_row; i++) {
        for (int j = 0; j < P; j++) {
            double sum = 0.0;
            for (int k = 0; k < M; k++) {
                sum += getMatrixElement(matrixA, i, k) * getMatrixElement(matrixB, k, j);
            }
            setMatrixElement(matrixC, i, j, sum);
        }
    }
}

// Function to clean up shared memory
void cleanupSharedMemory(const string& shm_name, void* ptr, size_t size) {
    munmap(ptr, size);
    shm_unlink(shm_name.c_str());
}

// Function to multiply matrices in parallel
vector<vector<double>> multiplyMatricesParallel(const vector<vector<double>>& A, 
                                               const vector<vector<double>>& B, 
                                               int num_processes) {
    int N = A.size();
    int M = A[0].size();
    int P = B[0].size();
    
    // Adjust number of processes if needed
    if (num_processes > N) {
        cout << "Warning: Number of processes reduced to number of rows in the result matrix (" << N << ")" << endl;
        num_processes = N;
    }
    
    // Create shared memory for matrices
    void* shm_A = createSharedMatrix(A, "/matrix_A");
    void* shm_B = createSharedMatrix(B, "/matrix_B");
    
    // Create shared memory for result matrix C
    size_t C_data_size = N * P * sizeof(double);
    size_t C_total_size = sizeof(SharedMatrixData) + C_data_size;
    
    int shm_C_fd = shm_open("/matrix_C", O_CREAT | O_RDWR, 0666);
    if (shm_C_fd == -1) {
        cerr << "Error creating shared memory for C: " << strerror(errno) << endl;
        exit(1);
    }
    
    if (ftruncate(shm_C_fd, C_total_size) == -1) {
        cerr << "Error setting size of shared memory for C: " << strerror(errno) << endl;
        exit(1);
    }
    
    void* shm_C = mmap(0, C_total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_C_fd, 0);
    if (shm_C == MAP_FAILED) {
        cerr << "Error mapping shared memory for C: " << strerror(errno) << endl;
        exit(1);
    }
    
    // Initialize C metadata
    SharedMatrixData* C_metadata = static_cast<SharedMatrixData*>(shm_C);
    C_metadata->n_rows = N;
    C_metadata->n_cols = P;
    
    // Calculate rows per process
    int rows_per_process = N / num_processes;
    int remaining_rows = N % num_processes;
    
    // Fork processes to perform multiplication
    pid_t pid;
    vector<pid_t> child_pids;
    
    for (int i = 0; i < num_processes; i++) {
        pid = fork();
        
        if (pid < 0) {
            cerr << "Error: Fork failed" << endl;
            // Clean up before exiting
            cleanupSharedMemory("/matrix_A", shm_A, sizeof(SharedMatrixData) + N * M * sizeof(double));
            cleanupSharedMemory("/matrix_B", shm_B, sizeof(SharedMatrixData) + M * P * sizeof(double));
            cleanupSharedMemory("/matrix_C", shm_C, C_total_size);
            exit(1);
        }
        else if (pid == 0) {
            // Child process
            int start_row = i * rows_per_process + min(i, remaining_rows);
            int end_row = (i + 1) * rows_per_process + min(i + 1, remaining_rows);
            
            calculateMatrixPortion(shm_A, shm_B, shm_C, start_row, end_row);
            
            // Child process exits after calculation
            exit(0);
        }
        else {
            // Parent process
            child_pids.push_back(pid);
        }
    }
    
    // Parent waits for all child processes to complete
    for (pid_t child_pid : child_pids) {
        int status;
        waitpid(child_pid, &status, 0);
    }
    
    // Extract result matrix from shared memory
    vector<vector<double>> C = extractMatrix(shm_C);
    
    // Clean up shared memory
    cleanupSharedMemory("/matrix_A", shm_A, sizeof(SharedMatrixData) + N * M * sizeof(double));
    cleanupSharedMemory("/matrix_B", shm_B, sizeof(SharedMatrixData) + M * P * sizeof(double));
    cleanupSharedMemory("/matrix_C", shm_C, C_total_size);
    
    return C;
}

void printUsage(const char* programName) {
    cout << "Usage: " << programName << " <matrix_A_file> <matrix_B_file> [options]" << endl;
    cout << "Options:" << endl;
    cout << "  -n <num_processes>   Number of processes to use (default: 1, sequential)" << endl;
    cout << "  -o <output_file>     Output file name (default: output.txt)" << endl;
    cout << endl;
    cout << "Examples:" << endl;
    cout << "  " << programName << " matrix_A.txt matrix_B.txt -n 4 -o result.txt" << endl;
    cout << "  " << programName << " matrix_A.txt matrix_B.txt -o result.txt" << endl;
    cout << "  " << programName << " matrix_A.txt matrix_B.txt" << endl;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        printUsage(argv[0]);
        return 1;
    }

    string fileA = argv[1];
    string fileB = argv[2];
    int num_processes = -1;

    int opt;
    while ((opt = getopt(argc - 2, argv + 2, "n:")) != -1) {
        switch (opt) {
            case 'n':
                num_processes = atoi(optarg);
                if (num_processes <= 0) {
                    cerr << "Error: Number of processes must be positive" << endl;
                    return 1;
                }
                break;
            default:
                printUsage(argv[0]);
                return 1;
        }
    }

    if (num_processes == -1) {
        cerr << "Error: -n <num_processes> is required." << endl;
        printUsage(argv[0]);
        return 1;
    }

    string output_folder = "output_" + to_string(num_processes);
    string result_file_seq = output_folder + "/C_seq.txt";
    string result_file_par = output_folder + "/C_parallel_" + to_string(num_processes) + ".txt";
    string log_file = output_folder + "/C.log.txt";

    if (!fs::exists(output_folder)) {
        fs::create_directory(output_folder);
    }

    ofstream log_stream(log_file);
    if (!log_stream.is_open()) {
        cerr << "Error: Could not open log file." << endl;
        return 1;
    }

    int N, M, P, M_B;
    vector<vector<double>> A = readMatrix(fileA, N, M);
    vector<vector<double>> B = readMatrix(fileB, M_B, P);

    if (M != M_B) {
        cerr << "Error: Incompatible matrix dimensions for multiplication" << endl;
        cerr << "Matrix A: " << N << "x" << M << endl;
        cerr << "Matrix B: " << M_B << "x" << P << endl;
        return 1;
    }

    // Sequential multiplication
    auto start_seq = chrono::high_resolution_clock::now();
    vector<vector<double>> C_seq = multiplyMatricesSequential(A, B);
    auto end_seq = chrono::high_resolution_clock::now();
    chrono::duration<double> seq_time = end_seq - start_seq;

    // Parallel multiplication
    auto start_par = chrono::high_resolution_clock::now();
    vector<vector<double>> C_par = multiplyMatricesParallel(A, B, num_processes);
    auto end_par = chrono::high_resolution_clock::now();
    chrono::duration<double> par_time = end_par - start_par;

    // Write result matrices
    writeMatrix(result_file_seq, C_seq);
    writeMatrix(result_file_par, C_par);

    // Logging
    log_stream << fixed << setprecision(6);
    cout << fixed << setprecision(6);
    log_stream << "Sequential time: " << seq_time.count() << " seconds" << endl;
    log_stream << "Parallel time (" << num_processes << " processes): " << par_time.count() << " seconds" << endl;
    log_stream << "Speedup: " << (seq_time.count() / par_time.count()) << endl;

    cout << "Sequential time: " << seq_time.count() << " seconds" << endl;
    cout << "Parallel time (" << num_processes << " processes): " << par_time.count() << " seconds" << endl;
    cout << "Speedup: " << (seq_time.count() / par_time.count()) << endl;

    log_stream.close();
    return 0;
}