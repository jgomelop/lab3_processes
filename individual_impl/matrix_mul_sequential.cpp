#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <string>
#include <sstream>

using namespace std;

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

// Function to multiply matrices
vector<vector<double>> multiplyMatrices(const vector<vector<double>>& A, const vector<vector<double>>& B) {
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

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <matrix_A_file> <matrix_B_file>" << endl;
        return 1;
    }
    
    string fileA = argv[1];
    string fileB = argv[2];
    
    int N, M, P, temp;
    
    // Read matrices
    auto start_read = chrono::high_resolution_clock::now();
    vector<vector<double>> A = readMatrix(fileA, N, M);
    vector<vector<double>> B = readMatrix(fileB, temp, P);
    auto end_read = chrono::high_resolution_clock::now();
    
    // Check if matrices can be multiplied
    if (M != temp) {
        cerr << "Error: Incompatible matrix dimensions for multiplication" << endl;
        return 1;
    }
    
    cout << "Matrix A: " << N << "x" << M << endl;
    cout << "Matrix B: " << M << "x" << P << endl;
    
    // Multiply matrices
    auto start_mult = chrono::high_resolution_clock::now();
    vector<vector<double>> C = multiplyMatrices(A, B);
    auto end_mult = chrono::high_resolution_clock::now();
    
    // Write result to file
    auto start_write = chrono::high_resolution_clock::now();
    writeMatrix("C.txt", C);
    auto end_write = chrono::high_resolution_clock::now();
    
    // Calculate and print execution times
    chrono::duration<double, milli> read_time = end_read - start_read;
    chrono::duration<double, milli> mult_time = end_mult - start_mult;
    chrono::duration<double, milli> write_time = end_write - start_write;
    chrono::duration<double, milli> total_time = read_time + mult_time + write_time;
    
    cout << "\nPerformance Metrics (Sequential Execution):" << endl;
    cout << "Read time: " << read_time.count() << " ms" << endl;
    cout << "Multiplication time: " << mult_time.count() << " ms" << endl;
    cout << "Write time: " << write_time.count() << " ms" << endl;
    cout << "Total time: " << total_time.count() << " ms" << endl;
    
    return 0;
}