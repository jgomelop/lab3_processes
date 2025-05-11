# University of Antioquia - Operating Systems Course  
# Practice #3 - Matrix Multiplication Using Processes  

Team Members:  
- Juan Pablo Gómez López - 1037665653
- Danilo Antonio Tovar Arias - 1193578670


# Mecanismo IPC
Mecanismo IPC: Shared Memory

Razón: Ninguna en particular. La escogimos al azar.

<details>
  <summary>C++</summary>

# Resultados
Los resultados se pueden ver en el notebook [informe.ipynb](notebooks/informe.ipynb), en la carpeta notebooks. Sin embargo, se resume el proceso y se expone los resultados a continuación.

## Proceso
Se ejecutó el programa [matrix_mul](matrix_mul) para las matrices A_big.txt y B_big.txt, con los valores [2 5 10 20 35 55 80 100] en la cantidad de procesos. Esto generó un archivo csv con los tiempos totales de ejecución por cada proceso.

En cada iteración, el programa [matrix_mul](matrix_mul) calcula el resultado de la multiplcación, el tiempo de cálculo para un solo proceso (sequential) y para n procesos (parallel). 

Este proceso se automatizó en el script [run_experiment.sh](cpp_outputs/run_experiment.sh), el cual generó todos los resultados de la multiplicación con sus logs (las impresiones en consola) por cada iteración junto con el archivo csv que es una tabla comparativa con los tiempos. Todo esto se encuentra en la carpeta cpp_outputs.

A continuación se muestran las figuras que exponen visualmente los resultados.

## Speedup vs Processes

![Speedup vs Processes](figs/speedup_vs_processes.png)

En este gráfico se observa como el speedup mejora abruptamente añadiendo unos pocos procesos. Sin embargo,  luego de 10 procesos, el speedup se mantiene constante.

## Parallel Time vs Processes
![Parallel Time vs Processes](figs/parallel_time_vs_processes.png)

Se observa una caída abrupta del tiempo de ejecución al aumentar hasta 10 procesos. Posterior a esta cantidad, el tiempo permanece constante.

## Execution Time Comparison
![Execution Time Comparison](figs/execution_time_comparison.png)

Se compara la ejecución Secuencial con la Paralela. Se observa que la paralela es mejor (menor tiempo de ejecución) en todos los casos.
</details>

<details>
  <summary>Go</summary>
  
## Proceso
Se ejecutó el programa [GoMatrixMult](GoMatrixMult.go) para las matrices A_big.txt y B_big.txt, con los valores [2 5 10 20 35 55 80 100] en la cantidad de procesos.

En cada iteración, el programa [GoMatrixMult](GoMatrixMult.go) calcula el resultado de la multiplcación, el tiempo de cálculo para un solo proceso (sequential) y para N procesos (parallel), así como el speedup que se generó, todos estos elementos se almacenaron para cada prueba en la carpeta [go_outputs](go_outputs).

A continuación se muestran las figuras que exponen visualmente los resultados.

## Speedup vs Processes

![Speedup vs Processes](https://github.com/user-attachments/assets/01ae6557-5256-4c6c-a691-964a59aa28d4)

En este gráfico se observa como el speedup se bueno cuando la cantidad de procesos se encuentra alrededor de 5 o 10, pues se encuentra en valores superiores a 1, y a pesar que inicialmente parece mejorar, despues de 10 procesos, el speedup disminuye drasticamente.

## Parallel Time vs Processes
![Parallel Time vs Processes](https://github.com/user-attachments/assets/a707d443-7798-47a8-b34e-37a5af10b72b)

Inicialmente disminuye cuando se usan mas de 2 procesos. Sin embargo, se observa un aumento significativo en los tiempos de ejecución al aumentar la cantidad de procesos posterior a 10 procesos.

## Execution Time Comparison
![Execution Time Comparison](https://github.com/user-attachments/assets/2f01495d-6ed7-4560-9180-fad574c8e206)

Cuando se compara la ejecución Secuencial con la Paralela, se observa que la paralela es mejor (menor tiempo de ejecución) cuando la cantidad procesos se encuentran alrededor de 5 o 10, pero es significativamente peor cuando se superan los 10 procesos.
  
</details>

