# Multiplicación de Matrices Usando Procesos

## Universidad de Antioquia - Curso de Sistemas Operativos  
**Práctica #3 - Multiplicación de Matrices Usando Procesos**  

### Integrantes:  
- Juan Pablo Gómez López - 1037665653  
- Danilo Tovar - 1193578670  

---

## Descripción
Este proyecto implementa la multiplicación de matrices utilizando procesos y memoria compartida como mecanismo de comunicación entre procesos (IPC). El objetivo es comparar el rendimiento de la multiplicación secuencial y paralela de matrices variando la cantidad de procesos.

---

## Compilación

```bash
g++ -o matrix_mul matrix_mul.cpp -lrt -pthread
```
Esto generará un archivo ejecutable llamado matrix_mul.

## Uso

```bash
./matrix_mul ./test_data/A_big.txt ./test_data/B_big.txt -n <CANTIDAD_PROCESOS>
```

Esto generará una carpeta llamada `output_CANTIDAD_PROCESOS` donde se encuentran tres archivos: el resultado de la multiplicación de matrices tanto secuencial como paralela, y el log que simplemente es lo que se imprime en pantalla al ejecutar el programa. 

## Reporte

El reporte de los resultados, que incluye descripción del proceso realizado y otros archivos y carpetas, se encuentra en el archivo [report.md](report.md)






