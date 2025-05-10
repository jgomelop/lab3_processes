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

using namespace std;

// Define a struct to store shared memory metadata
struct SharedMatrixData {
    int n_rows;
    int n_cols;
    // The actual matrix data will be stored after this struct in memory
};

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
    
    int rows = matrix.size();
    int cols = matrix[0].size();
    
    // Write matrix elements - each element separated by spaces, each row on a new line
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
    // Check for minimum required arguments
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }
    
    string fileA = argv[1];
    string fileB = argv[2];
    
    // Default values
    int num_processes = 1;
    string output_file = "output.txt";
    
    // Parse optional arguments
    int opt;
    while ((opt = getopt(argc, argv, "n:o:h")) != -1) {
        switch (opt) {
            case 'n':
                num_processes = atoi(optarg);
                if (num_processes <= 0) {
                    cerr << "Error: Number of processes must be positive" << endl;
                    return 1;
                }
                break;
            case 'o':
                output_file = optarg;
                break;
            case 'h':
                printUsage(argv[0]);
                return 0;
            default:
                printUsage(argv[0]);
                return 1;
        }
    }
    
    int N, M, P, M_B;
    
    // Start timing
    auto start_total = chrono::high_resolution_clock::now();
    
    // Read matrices
    auto start_read = chrono::high_resolution_clock::now();
    vector<vector<double>> A = readMatrix(fileA, N, M);
    vector<vector<double>> B = readMatrix(fileB, M_B, P);
    auto end_read = chrono::high_resolution_clock::now();
    
    // Check if matrices can be multiplied
    if (M != M_B) {
        cerr << "Error: Incompatible matrix dimensions for multiplication" << endl;
        cerr << "Matrix A: " << N << "x" << M << endl;
        cerr << "Matrix B: " << M_B << "x" << P << endl;
        return 1;
    }
    
    cout << "Matrix A: " << N << "x" << M << endl;
    cout << "Matrix B: " << M << "x" << P << endl;
    
    vector<vector<double>> C;
    string implementation_type;
    
    // Start multiplication timing
    auto start_mult = chrono::high_resolution_clock::now();
    
    // Choose between sequential and parallel implementation
    if (num_processes == 1) {
        cout << "Using sequential implementation" << endl;
        implementation_type = "Sequential";
        C = multiplyMatricesSequential(A, B);
    } else {
        cout << "Using parallel implementation with " << num_processes << " processes" << endl;
        implementation_type = "Parallel";
        C = multiplyMatricesParallel(A, B, num_processes);
    }
    
    // End multiplication timing
    auto end_mult = chrono::high_resolution_clock::now();
    
    // Write result to file
    auto start_write = chrono::high_resolution_clock::now();
    writeMatrix(output_file, C);
    auto end_write = chrono::high_resolution_clock::now();
    
    // Calculate and print execution times
    auto end_total = chrono::high_resolution_clock::now();
    
    chrono::duration<double, milli> read_time = end_read - start_read;
    chrono::duration<double, milli> mult_time = end_mult - start_mult;
    chrono::duration<double, milli> write_time = end_write - start_write;
    chrono::duration<double, milli> total_time = end_total - start_total;
    
    cout << "\nPerformance Metrics (" << implementation_type << " Execution";
    if (num_processes > 1) cout << " with " << num_processes << " processes";
    cout << "):" << endl;
    cout << "Read time: " << read_time.count() << " ms" << endl;
    cout << "Multiplication time: " << mult_time.count() << " ms" << endl;
    cout << "Write time: " << write_time.count() << " ms" << endl;
    cout << "Total time: " << total_time.count() << " ms" << endl;
    cout << "Result written to: " << output_file << endl;
    
    return 0;
}