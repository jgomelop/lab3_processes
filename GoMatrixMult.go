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
    "path/filepath"
    "strconv"
    "strings"
    "sync"
    "time"

    "golang.org/x/sys/unix"
)

// Tamaño en bytes de un float64.
const elementSize = 8

// ------------------ Lectura y escritura de matrices ------------------

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

// writeMatrix escribe la matriz en un archivo formateando cada elemento con 14 decimales.
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

// ------------------ Multiplicación de matrices ------------------

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

// multiplyPartial calcula el producto parcial para las filas de A en el rango [startRow, endRow).
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

// ------------------ Manejo de memoria compartida vía MMap ------------------

// createSharedMemoryFile crea (o trunca) un archivo de tamaño "size" que usaremos para la memoria compartida.
func createSharedMemoryFile(filename string, size int) error {
    f, err := os.Create(filename)
    if err != nil {
        return err
    }
    defer f.Close()
    return f.Truncate(int64(size))
}

// openSharedMemoryMmap abre el archivo indicado y lo mapea en memoria.
func openSharedMemoryMmap(filename string) ([]byte, error) {
    f, err := os.OpenFile(filename, os.O_RDWR, 0666)
    if err != nil {
        return nil, err
    }
    defer f.Close()

    fi, err := f.Stat()
    if err != nil {
        return nil, err
    }
    size := int(fi.Size())

    data, err := unix.Mmap(int(f.Fd()), 0, size, unix.PROT_READ|unix.PROT_WRITE, unix.MAP_SHARED)
    if err != nil {
        return nil, err
    }
    return data, nil
}

// ------------------ Proceso hijo ------------------
//
// Se invoca con los argumentos:
//   child <startRow> <endRow> <shmFilename> <P> <fileA> <fileB>
//
// Lee las matrices A y B, calcula la multiplicación parcial de las filas asignadas,
// y escribe el resultado en la región correspondiente del mapeo de memoria compartida.
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

    // Leer matrices A y B.
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

    // Calcular el producto parcial.
    partialResult := multiplyPartial(A, B, startRow, endRow)

    // Abrir la memoria compartida mediante MMap.
    shmData, err := openSharedMemoryMmap(shmFilename)
    if err != nil {
        fmt.Fprintln(os.Stderr, "Error abriendo memoria compartida (MMap):", err)
        os.Exit(1)
    }

    // Escribir cada elemento en la posición adecuada del mapeo.
    for i, row := range partialResult {
        globalRow := startRow + i
        for j, val := range row {
            offset := (globalRow*P + j) * elementSize
            binary.LittleEndian.PutUint64(shmData[offset:offset+elementSize], math.Float64bits(val))
        }
    }

    // Sincronizar los cambios (MS_SYNC obliga a la escritura inmediata).
    if err := unix.Msync(shmData, unix.MS_SYNC); err != nil {
        fmt.Fprintln(os.Stderr, "Error en Msync:", err)
        os.Exit(1)
    }
    unix.Munmap(shmData)
    os.Exit(0)
}

// ------------------ Proceso padre ------------------
//
// Usa flags para recibir:
//   -A : archivo para la matriz A
//   -B : archivo para la matriz B
//   -N : número de procesos (hijos) a utilizar
//
// El padre lee las matrices, ejecuta la multiplicación secuencial (baseline),
// crea el archivo de memoria compartida (que se mapea en cada proceso hijo),
// divide las filas de A entre los procesos hijos (cada uno ejecuta el modo "child")
// y, una vez que estos finalizan, lee el resultado de la memoria mapeada y lo escribe en archivos.
func runParentProcess() {
    // Definir flags.
    aFile := flag.String("A", "", "Archivo de entrada para la matriz A")
    bFile := flag.String("B", "", "Archivo de entrada para la matriz B")
    numProc := flag.Int("N", 1, "Número de procesos a utilizar")
    flag.Parse()

    if *aFile == "" || *bFile == "" {
        log.Fatal("Debe proporcionar los archivos de entrada para A y B usando -A y -B.")
    }

    // Leer matrices.
    A, err := readMatrix(*aFile)
    if err != nil {
        log.Fatal("Error leyendo la matriz A:", err)
    }
    B, err := readMatrix(*bFile)
    if err != nil {
        log.Fatal("Error leyendo la matriz B:", err)
    }

    // Verificar compatibilidad de dimensiones: columnas de A == filas de B.
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

    // --- Multiplicación en paralelo con memoria compartida (MMap) ---
    // El archivo de memoria compartida tendrá tamaño = N * P * 8 bytes.
    shmFilename := "shm.dat"
    shmSize := N * P * elementSize
    if err := createSharedMemoryFile(shmFilename, shmSize); err != nil {
        log.Fatal("Error creando archivo de memoria compartida:", err)
    }

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
            // Invocar el proceso hijo con:
            // "child" <startRow> <endRow> <shmFilename> <P> <fileA> <fileB>
            cmd := exec.Command(os.Args[0], "child",
                strconv.Itoa(sRow), strconv.Itoa(eRow),
                shmFilename, strconv.Itoa(P), *aFile, *bFile)
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

    // Leer los resultados de la memoria compartida mediante MMap.
    shmData, err := openSharedMemoryMmap(shmFilename)
    if err != nil {
        log.Fatal("Error abriendo memoria compartida con MMap:", err)
    }
    C_par := make([][]float64, N)
    for i := 0; i < N; i++ {
        C_par[i] = make([]float64, P)
        for j := 0; j < P; j++ {
            offset := (i*P + j) * elementSize
            bits := binary.LittleEndian.Uint64(shmData[offset : offset+elementSize])
            C_par[i][j] = math.Float64frombits(bits)
        }
    }
    unix.Munmap(shmData)

    // Crear el directorio de salida "GoResults".
    outputDir := "GoResults"
    if err := os.MkdirAll(outputDir, 0755); err != nil {
        log.Fatal("Error creando directorio de resultados:", err)
    }

    // Escribir la matriz del proceso paralelo en "GoResults/C_par.txt"
    outputPath := filepath.Join(outputDir, "C_par.txt")
    if err := writeMatrix(outputPath, C_par); err != nil {
        log.Fatal("Error escribiendo la matriz C_par:", err)
    }

    // Escribir la matriz del proceso secuencial en "GoResults/C_seq.txt"
    outputPath = filepath.Join(outputDir, "C_seq.txt")
    if err := writeMatrix(outputPath, C_seq); err != nil {
        log.Fatal("Error escribiendo la matriz C_seq:", err)
    }

    // Registrar los tiempos en log.txt.
    logFilePath := filepath.Join(outputDir, "log.txt")
    logFile, err := os.Create(logFilePath)
    if err != nil {
        log.Fatalf("Error creando archivo log: %v", err)
    }
<<<<<<< HEAD
    // Write the timing info.
    fmt.Fprintf(logFile, "Sequential time: %v\n", durationSeq)
    fmt.Fprintf(logFile, "Tiempo paralelo (con %d procesos): %v\n", numWorkers, durationPar)
||||||| parent of ab9143e (Update Go implementation and tests)
    // Write the timing info.
    fmt.Fprintf(logFile, "Sequential time: %v\n", durationSeq)
    fmt.Fprintf(logFile, "Parallel time: %v\n", durationPar)
=======
    fmt.Fprintf(logFile, "Tiempo secuencial: %v\n", durationSeq)
    fmt.Fprintf(logFile, "Tiempo paralelo (con %d procesos): %v\n", numWorkers, durationPar)
>>>>>>> ab9143e (Update Go implementation and tests)
    fmt.Fprintf(logFile, "Speedup: %.2fx\n", speedup)
    logFile.Close()

    // Limpiar: eliminar el archivo de memoria compartida.
    if err := os.Remove(shmFilename); err != nil {
        log.Printf("Error removiendo %s: %v\n", shmFilename, err)
    } else {
        log.Printf("%s eliminado correctamente.\n", shmFilename)
    }
}

func main() {
    // Si se invoca con el argumento "child", se ejecuta la rutina del proceso hijo.
    if len(os.Args) > 1 && os.Args[1] == "child" {
        runChildProcess()
        return
    }
    // Caso contrario, se ejecuta el proceso padre.
    runParentProcess()
}
