#!/bin/bash

# Check if two arguments (txt files) are provided
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <matrix_A.txt> <matrix_B.txt>"
    exit 1
fi

# Assign arguments to variables
MATRIX_A="$1"
MATRIX_B="$2"
OUTPUT_ROOT="cpp_outputs"

# Create output directory
mkdir -p "$OUTPUT_ROOT"

# CSV header
CSV_FILE="$OUTPUT_ROOT/results.csv"
echo "Processes,SequentialTime(s),ParallelTime(s),Speedup" > "$CSV_FILE"

# Process counts to test
process_counts=(2 5 10 20 35 55 80 100)

for n in "${process_counts[@]}"; do
    echo "Running with $n processes..."
    ./matrix_mul "$MATRIX_A" "$MATRIX_B" -n "$n"  # Pass matrix files as arguments

    folder="output_$n"
    if [ -d "$folder" ]; then
        mv "$folder" "$OUTPUT_ROOT/"

        # Parse log for timing values
        LOG="$OUTPUT_ROOT/$folder/C.log.txt"
        if [ -f "$LOG" ]; then
            seq_time=$(grep "Sequential time" "$LOG" | awk '{print $3}')
            par_time=$(grep "Parallel time" "$LOG" | awk '{print $5}')
            speedup=$(grep "Speedup" "$LOG" | awk '{print $2}')
            echo "$n,$seq_time,$par_time,$speedup" >> "$CSV_FILE"
        else
            echo "Warning: Log file not found for $n processes"
        fi
    else
        echo "Warning: Folder '$folder' not found!"
    fi
done
