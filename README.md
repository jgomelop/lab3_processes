# Multiplicación de Matrices Usando Procesos

## Universidad de Antioquia - Curso de Sistemas Operativos  
**Práctica #3 - Multiplicación de Matrices Usando Procesos**  

### Integrantes:  
- Juan Pablo Gómez López - 1037665653  
- Danilo Antonio Tovar Arias - 1193578670  

---

## Descripción
Este proyecto implementa la multiplicación de matrices utilizando procesos y memoria compartida como mecanismo de comunicación entre procesos (IPC). El objetivo es comparar el rendimiento de la multiplicación secuencial y paralela de matrices variando la cantidad de procesos.

---

## Compilación
<details>
  <summary>C++</summary>
  
  ```bash
  g++ -o matrix_mul matrix_mul.cpp -lrt -pthread
  ```
Esto generará un archivo ejecutable llamado matrix_mul.
</details>

<details>
  <summary>Go</summary>
  
  ```bash
  go build GoMatrixMult.go
  ```
Esto generará un archivo ejecutable llamado GoMatrixMult.
</details>

## Uso
<details>
  <summary>C++</summary>
  
```bash
./matrix_mul ./test_data/A_big.txt ./test_data/B_big.txt -n <CANTIDAD_PROCESOS>
```

Esto generará una carpeta llamada `output_CANTIDAD_PROCESOS` donde se encuentran tres archivos: el resultado de la multiplicación de matrices tanto secuencial como paralela, y el log que simplemente es lo que se imprime en pantalla al ejecutar el programa.
</details>

<details>
  <summary>Go</summary>
  
  ```bash
  ./GoMatrixMult -A ./test_data/A_big.txt -B ./test_data/B_big.txt -N <CANTIDAD_PROCESOS>
  ```

Esto generará una carpeta llamada `GoResults` con dos archivos, un archivo llamado `C_par.txt` donde se encuentra el resultado de la multiplicación paralela, y un archivo llamado `C_seq.txt` donde se encuentra el resultado de la multiplicación secuencial.
</details>

## Reporte

El reporte de los resultados, que incluye descripción del proceso realizado y otros archivos y carpetas, se encuentra en el archivo [report.md](report.md)






