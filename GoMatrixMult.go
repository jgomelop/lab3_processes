package main

import (
    "bufio"
    "encoding/binary"
    "flag"
    "fmt"
    "log"
    "math"
    "os"
    "os/exec"
    "strconv"
    "strings"
    "sync"
    "time"
    "path/filepath"
)

// Tamaño en bytes de un float64
const elementSize = 8

// ------------------ Funciones para lectura y escritura de matrices ------------------

// readMatrix lee una matriz desde un archivo y devuelve una matriz (slice 2D de float64).
func readMatrix(filename string) ([][]float64, error) {
    file, err := os.Open(filename)
    if err != nil {
        return nil, err
    }
    defer file.Close()

    var matrix [][]float64
    scanner := bufio.NewScanner(file)
    for scanner.Scan() {
        line := strings.TrimSpace(scanner.Text())
        if line == "" {
            continue
        }
        fields := strings.Fields(line)
        var row []float64
        for _, field := range fields {
            val, err := strconv.ParseFloat(field, 64)
            if err != nil {
                return nil, err
            }
            row = append(row, val)
        }
        matrix = append(matrix, row)
    }
    if err := scanner.Err(); err != nil {
        return nil, err
    }
    return matrix, nil
}

// writeMatrix escribe la matriz en un archivo formateando cada elemento con 2 decimales.
func writeMatrix(filename string, matrix [][]float64) error {
    file, err := os.Create(filename)
    if err != nil {
        return err
    }
    defer file.Close()

    writer := bufio.NewWriter(file)
    for _, row := range matrix {
        for _, val := range row {
            fmt.Fprintf(writer, "%.14f ", val)
        }
        writer.WriteString("\n")
    }
    return writer.Flush()
}

// ------------------ Función de multiplicación secuencial ------------------

// sequentialMultiply realiza la multiplicación clásica de matrices (A * B) y devuelve la matriz resultado.
func sequentialMultiply(A, B [][]float64) [][]float64 {
    N := len(A)
    M := len(A[0])
    P := len(B[0])
    C := make([][]float64, N)
    for i := 0; i < N; i++ {
        C[i] = make([]float64, P)
        for j := 0; j < P; j++ {
            var sum float64
            for k := 0; k < M; k++ {
                sum += A[i][k] * B[k][j]
            }
            C[i][j] = sum
        }
    }
    return C
}

// ------------------ Función para multiplicación parcial (subconjunto de filas) ------------------

// multiplyPartial calcula el producto de matrices para las filas de A en el rango [startRow, endRow).
func multiplyPartial(A, B [][]float64, startRow, endRow int) [][]float64 {
    M := len(A[0])
    P := len(B[0])
    numRows := endRow - startRow
    partial := make([][]float64, numRows)
    for i := 0; i < numRows; i++ {
        partial[i] = make([]float64, P)
    }
    for i := startRow; i < endRow; i++ {
        for j := 0; j < P; j++ {
            var sum float64
            for k := 0; k < M; k++ {
                sum += A[i][k] * B[k][j]
            }
            partial[i-startRow][j] = sum
        }
    }
    return partial
}

// ------------------ Función para crear "memoria compartida" (archivo) ------------------

// createSharedMemory crea un archivo de tamaño "size" que servirá como memoria compartida.
func createSharedMemory(filename string, size int) (*os.File, error) {
    file, err := os.Create(filename)
    if err != nil {
        return nil, err
    }
    if err := file.Truncate(int64(size)); err != nil {
        file.Close()
        return nil, err
    }
    return file, nil
}

// ------------------ Función para el proceso hijo ------------------
//
// El proceso hijo se invoca con los argumentos:
//    child <startRow> <endRow> <shmFilename> <P> <fileA> <fileB>
//
// Lee las matrices A y B, calcula la multiplicación parcial para las filas asignadas,
// y escribe el resultado en el archivo de memoria compartida en la región correspondiente.
func runChildProcess() {
    if len(os.Args) < 8 {
        fmt.Fprintln(os.Stderr, "Uso: <program> child <startRow> <endRow> <shmFilename> <P> <fileA> <fileB>")
        os.Exit(1)
    }
    startRow, err := strconv.Atoi(os.Args[2])
    if err != nil {
        fmt.Fprintln(os.Stderr, "Error parseando startRow:", err)
        os.Exit(1)
    }
    endRow, err := strconv.Atoi(os.Args[3])
    if err != nil {
        fmt.Fprintln(os.Stderr, "Error parseando endRow:", err)
        os.Exit(1)
    }
    shmFilename := os.Args[4]
    P, err := strconv.Atoi(os.Args[5])
    if err != nil {
        fmt.Fprintln(os.Stderr, "Error parseando P:", err)
        os.Exit(1)
    }
    fileA := os.Args[6]
    fileB := os.Args[7]

    // Leer matrices A y B
    A, err := readMatrix(fileA)
    if err != nil {
        fmt.Fprintln(os.Stderr, "Error leyendo A:", err)
        os.Exit(1)
    }
    B, err := readMatrix(fileB)
    if err != nil {
        fmt.Fprintln(os.Stderr, "Error leyendo B:", err)
        os.Exit(1)
    }

    // Calcular el producto parcial para las filas [startRow, endRow)
    partialResult := multiplyPartial(A, B, startRow, endRow)

    // Abrir el archivo de memoria compartida (modo lectura-escritura)
    f, err := os.OpenFile(shmFilename, os.O_RDWR, 0666)
    if err != nil {
        fmt.Fprintln(os.Stderr, "Error abriendo el archivo de memoria compartida:", err)
        os.Exit(1)
    }
    defer f.Close()

    // Escribir cada valor en la posición correspondiente del archivo
    // Cada float64 ocupa 8 bytes; se dispone la matriz de forma "row-major"
    for i, row := range partialResult {
        globalRow := startRow + i
        for j, val := range row {
            offset := int64((globalRow*P + j) * elementSize)
            buf := make([]byte, elementSize)
            binary.LittleEndian.PutUint64(buf, math.Float64bits(val))
            _, err := f.WriteAt(buf, offset)
            if err != nil {
                fmt.Fprintln(os.Stderr, "Error escribiendo en memoria compartida:", err)
                os.Exit(1)
            }
        }
    }
    os.Exit(0)
}

// ------------------ Función para el proceso padre ------------------
//
// Se usan flags para recibir:
//   -A : archivo para la matriz A
//   -B : archivo para la matriz B
//   -N : número de procesos (hijos) a utilizar
//
// El padre lee las matrices, realiza la multiplicación secuencial (para comparar tiempos),
// crea el archivo de memoria compartida, divide las filas de A entre los procesos hijos
// (cada proceso ejecuta el modo "child" y escribe sobre la zona que le corresponde),
// espera a que todos finalicen, lee el resultado y lo escribe en "C.txt".
func runParentProcess() {
    // Definir flags
    aFile := flag.String("A", "", "Archivo de entrada para la matriz A")
    bFile := flag.String("B", "", "Archivo de entrada para la matriz B")
    numProc := flag.Int("N", 1, "Número de procesos a utilizar")
    flag.Parse()

    if *aFile == "" || *bFile == "" {
        log.Fatal("Debe proporcionar los archivos de entrada para A y B usando -A y -B.")
    }

    // Leer las matrices
    A, err := readMatrix(*aFile)
    if err != nil {
        log.Fatal("Error leyendo la matriz A:", err)
    }
    B, err := readMatrix(*bFile)
    if err != nil {
        log.Fatal("Error leyendo la matriz B:", err)
    }

    // Verificar dimensiones: el número de columnas de A debe ser igual al número de filas de B.
    if len(A) == 0 || len(B) == 0 || len(A[0]) != len(B) {
        log.Fatal("Dimensiones incompatibles entre A y B.")
    }
    N := len(A)
    M := len(A[0])
    P := len(B[0])
    fmt.Printf("Se multiplicarán matrices A(%d x %d) * B(%d x %d)\n", N, M, len(B), P)

    // --- Multiplicación secuencial (baseline) ---
    startSeq := time.Now()
    C_seq := sequentialMultiply(A, B)
    durationSeq := time.Since(startSeq)
    fmt.Printf("Tiempo secuencial: %v\n", durationSeq)


    // --- Multiplicación en paralelo con memoria compartida ---
    // El archivo de "memoria compartida" tendrá tamaño = N * P * 8 bytes.
    shmFilename := "shm.dat"
    shmSize := N * P * elementSize
    shmFile, err := createSharedMemory(shmFilename, shmSize)
    if err != nil {
        log.Fatal("Error creando memoria compartida:", err)
    }
    // Cerramos el archivo, ya que los procesos hijos lo abrirán según se requiera.
    shmFile.Close()

    // Dividir las filas de A entre los procesos hijos.
    numWorkers := *numProc
    rowsPerWorker := N / numWorkers
    remainder := N % numWorkers

    var wg sync.WaitGroup
    startPar := time.Now()
    for i := 0; i < numWorkers; i++ {
        startRow := i * rowsPerWorker
        endRow := startRow + rowsPerWorker
        if i < remainder {
            startRow += i
            endRow += i + 1
        } else {
            startRow += remainder
            endRow += remainder
        }

        wg.Add(1)
        go func(sRow, eRow int) {
            defer wg.Done()
            // Invocar el proceso hijo: se pasan los parámetros:
            // "child" <startRow> <endRow> <shmFilename> <P> <fileA> <fileB>
            cmd := exec.Command(os.Args[0], "child",
                strconv.Itoa(sRow), strconv.Itoa(eRow),
                shmFilename, strconv.Itoa(P), *aFile, *bFile)
            // Se espera a que el hijo termine
            if err := cmd.Run(); err != nil {
                log.Printf("Error en proceso hijo (%d-%d): %v\n", sRow, eRow, err)
            }
        }(startRow, endRow)
    }
    wg.Wait()
    durationPar := time.Since(startPar)
    fmt.Printf("Tiempo paralelo (con %d procesos): %v\n", numWorkers, durationPar)
    speedup := float64(durationSeq) / float64(durationPar)
    fmt.Printf("Speedup: %.2fx\n", speedup)

    // Leer la matriz resultado desde el archivo de memoria compartida.
    resultFile, err := os.Open(shmFilename)
    if err != nil {
        log.Fatal("Error abriendo la memoria compartida para leer los resultados:", err)
    }

    C_par := make([][]float64, N)
    for i := 0; i < N; i++ {
        C_par[i] = make([]float64, P)
        for j := 0; j < P; j++ {
            offset := int64((i*P + j) * elementSize)
            buf := make([]byte, elementSize)
            _, err := resultFile.ReadAt(buf, offset)
            if err != nil {
                log.Fatal("Error leyendo desde la memoria compartida:", err)
            }
            bits := binary.LittleEndian.Uint64(buf)
            C_par[i][j] = math.Float64frombits(bits)
        }
    }

    // Create the output directory "results"
    outputDir := "GoResults"
    if err := os.MkdirAll(outputDir, 0755); err != nil {
    	log.Fatal("Error creating output directory:", err)
    }

    // Construct the full file path.
    outputPath := filepath.Join(outputDir, "C_par.txt")

    // Escribir la matriz resultado en "GoResults/C_par.txt"
    if err := writeMatrix(outputPath, C_par); err != nil {
        log.Fatal("Error escribiendo la matriz C:", err)
    }

    // Construct the full file path.
    outputPath = filepath.Join(outputDir, "C_seq.txt")

    // Escribir la matriz resultado del proceso secuencial en "GoResults/C_seq.txt"
    if err := writeMatrix(outputPath, C_seq); err != nil {
        log.Fatal("Error escribiendo la matriz C:", err)
    }

    // --- Log the timings to log.txt ---
    logFilePath := filepath.Join(outputDir, "log.txt")
    logFile, err := os.Create(logFilePath)
    if err != nil {
        log.Fatalf("Error creating log file: %v", err)
    }
    // Write the timing info.
    fmt.Fprintf(logFile, "Sequential time: %v\n", durationSeq)
    fmt.Fprintf(logFile, "Tiempo paralelo (con %d procesos): %v\n", numWorkers, durationPar)
    fmt.Fprintf(logFile, "Speedup: %.2fx\n", speedup)
    logFile.Close() // Ensure it's flushed and closed

    // Close the result file before calling os.Remove
    resultFile.Close()

    // Limpiar (eliminar el archivo de memoria compartida)
    err = os.Remove(shmFilename)
    if err != nil {
	log.Printf("Error removing %s: %v\n", shmFilename, err)
    } else {
	log.Printf("%s successfully removed.\n", shmFilename)
    }
}

func main() {
    // Si se invoca con el argumento "child", se ejecuta la rutina del proceso hijo.
    if len(os.Args) > 1 && os.Args[1] == "child" {
        runChildProcess()
        return
    }
    // De lo contrario, se ejecuta el proceso padre.
    runParentProcess()
}
