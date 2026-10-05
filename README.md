# Planificador Dieciochero

Tarea 1 de Sistemas Operativos. Simulador y planificador de actividades modeladas con un DAG (Grafo Acíclico Dirigido), desarrollado en **C++** (estándar C++17) para organizar las celebraciones del estimado señor Loyola.

El planificador coordina la ejecución concurrente de las actividades respetando en todo momento el límite de procesos simultáneos (K). Los insumos generados por cada actividad se transmiten mediante pipes, las ramas
dependientes de una actividad que falla se descartan sin detener al resto del plan, y la llegada imprevista de la autoridad sanitaria (la Seremi) se maneja mediante la señal POSIX SIGINT. Todo el sistema se basa en procesos independientes 
creados con `fork()` y sincronizados con llamadas al sistema, sin usar hilos ni mecanismos de sincronización de hilos.

## Modo de uso

```bash
./planificador plan.txt K [porcentajeFallo]
```

- `plan.txt`: archivo con las actividades (ver formato abajo).
- `K`: número máximo de procesos concurrentes (entero ≥ 1).
- `porcentajeFallo` (opcional, 0–100): probabilidad de que una actividad "falle" de forma simulada. Sirve para poder demostrar en la defensa que el aislamiento de errores funciona, sin depender de que algo falle de verdad.


Ejemplos:
```bash
./planificador plan.txt 2
./planificador plan.txt 3 40        # con 40% de fallo simulado
```

## Formato de plan.txt

```
ID_Actividad : Nombre_Actividad : Duración : Dependencias
```

Ejemplo:
```
1 : prender_carbon : 500 :
2 : comprar_carne : 1200 :
3 : comprar_pan : 300 :
4 : asar_longaniza : 800 : 1, 2
5 : armar_choripan : 250 : 3, 4
6 : servir_mesa : 100 : 5
```

- **ID_Actividad**: identificador único alfanumérico.
- **Nombre_Actividad**: nombre descriptivo de la actividad.
- **Duración**: tiempo en milisegundos. Si el campo viene vacío o con un  valor ≤ 0, se le asigna una duración aleatoria entre 100 y 5000 ms.
- **Dependencias**: IDs separados por coma de las actividades que deben terminar antes de que esta pueda iniciar. Como una actividad puede no tener dependencias este campo puede estar vacio.

## Compilación y ejecución

Desde la raíz del repositorio:

```bash
g++ -Wall -Wextra -std=c++17 -o planificador source/*.cpp -lpthread
```

```bash
./planificador plan.txt 2
```

## Estructura

El proyecto está organizado en la carpeta `source/`:

### `dag.hpp` / `dag.cpp`
Define la estructura del grafo y el parseo de `plan.txt`.

- `struct Activity`: una actividad tiene id, nombre, duración, sus dependencias y quién depende de ella (sucesores), cuántas dependencias le faltan por terminar (dep_pendientes), su estado actual, su pid mientras
  corre, el fd de lectura de su pipe de salida y el mensaje que le llegará de sus dependencias.
  
- `struct Dag`: contiene el vector de todas las Activity.

- `dag_load(Dag&, path)`: lee `plan.txt` línea a línea. En una primera pasada crea todas las actividades y registra sus IDs en un unordered_map para no tener que buscar por texto cada vez. En una segunda pasada resuelve las
  dependencias a índices de vector y arma la lista inversa sucesores, para que el planificador sepa a quién desbloquear cuando una actividad termina.
  
- `es_aciclico(const Dag&)`: corre el algoritmo de Kahn para confirmar que el grafo no tiene ciclos antes de empezar a ejecutar nada.

### `activity.hpp` / `activity.cpp`
Código que corre **dentro del proceso hijo** después del `fork()`.

- `ejecutar_actividad(...)`: lee el insumo que le mandó el padre por pipe, simula el trabajo durmiendo tiempo_ms con `nanosleep()` y al terminar escribe su resultado
   (`OK|...` o `FAIL|...`) en el otro pipe.
  
- `manejador_sigterm(int)`: manejador de SIGTERM, si el padre decide abortar esta actividad ya sea por un SIGINT o por una rama fallida, reporta un mensaje de aborto y termina con `_exit()`.
  
- `leer_mensaje` / `write_all`: funciones auxiliares de lectura/escritura bloqueante sobre un pipe, reintentando si la llamada es interrumpida por una señal EINTR.

### `scheduler.hpp` / `scheduler.cpp`
El planificador propiamente dicho.
- `lanzar_actividad(...)`: crea los dos pipes (uno para mandarle el insumo al hijo, otro para que el hijo reporte su resultado), hace fork(), y deja todo listo en el padre para que el loop principal espere el resultado más adelante.
  
- `abortar_dependientes(...)`: cuando una actividad falla, recorre de forma recursiva su lista de sucesores marcándolos como abortados y mandándoles SIGTERM si ya estaban corriendo. El propio estado de cada actividad actúa como
  marca de "ya visitado", así que no se procesa dos veces aunque el DAG tenga forma de diamante.

- `marcar_exito(...)`: es cuando una actividad termina bien, le suma su mensaje al insumo de cada sucesor y le resta 1 a su dep_pendientes.

- `apagar_por_sigint(Dag&)`: apagado ordenado ante Ctrl+C en terminal, manda SIGTERM a todas las actividades corriendo, las espera una por una y marca como abortada toda actividad que no alcanzó a empezar.
  
- `manejador_sigint(int)`: el manejador de SIGINT en sí solo pone en 1 una flag sigintRecibido. Toda la lógica real de apagado vive en apagar_por_sigint que corre en el loop principal, no dentro del manejador.
  
- `scheduler_run(Dag&, k, cfg)`: el ciclo principal. Mientras haya cupo (activos < K) recorre el arreglo lanzando toda actividad con dep_pendientes == 0, cuando no puede lanzar más, se bloquea en `waitpid(-1, &status, 0)`
  esperando a que cualquier hijo termine.

### `main.cpp`
Punto de entrada: valida los argumentos (`plan.txt`, `K` y el `porcentajeFallo` opcional), llama a `dag_load()` y después a `scheduler_run()`.



## Decisiones de diseño

### 1. IDs resueltos a índices numéricos en el parseo, no en cada consulta
Comprobar dependencias comparando strings con find/strcmp en cada consulta del planificador sería cada vez más caro a medida que crece el plan. Por eso dag_load() resuelve cada ID de texto a un índice numérico una sola vez, usando
un `unordered_map<string,int>` como tabla de búsqueda. Así, saber si una actividad está lista es solo comprobar dep_pendientes == 0 (O(1)), y cuando una actividad termina solo se recorren sus sucesores directos para restarles 1.

### 2. Paso de mensajes por pipes, en ambas direcciones
Cada actividad tiene dos pipes, uno padre → hijo por el que el padre le manda el insumo ya recolectado de sus dependencias antes de que empiece a trabajar, y otro hijo → padre por el que el hijo reporta su resultado 
`OK|id|nombre|detalle` o `FAIL|id|nombre|detalle` justo antes de terminar. Como el padre decide cuándo lanzar cada actividad, recién cuando sus dependencias ya terminaron, es quien naturalmente actúa de enrutador entre una actividad que
ya no existe como proceso y otra que todavía no se ha creado.

### 3. Sin espera activa (busy waiting)
El ciclo principal lanza todas las actividades que puede hasta llenar el cupo `K` y, cuando no puede lanzar más, se bloquea en:
```cpp
pid_t pid = waitpid(-1, &status, 0);
```
El proceso padre queda suspendido hasta que el sistema operativo le avisa que algún hijo terminó, no hay ningún sleep() en loop ni consulta en ninguna parte del planificador.

### 4. Aislamiento de errores con recursión, usando el propio estado como marca de visitado
Cuando `waitpid()` detecta que un hijo terminó con error, la actividad se marca FAILED y se llama a abortar_dependientes(), que recorre recursivamente todos sus sucesores marcándolos ABORTED. No hace falta un arreglo aparte de 
"visitados", si un sucesor ya no está en PENDING ni RUNNING la función no lo vuelve a tocar ni sigue recorriendo desde ahí, lo que también evita procesar dos veces el mismo nodo en un DAG con forma de diamante.

### 5. Vaciar el buffer de stdout antes de cada fork()
Cuando la salida no es una terminal stdio bufferiza completo en vez de por línea. Sin un fflush(stdout) justo antes de fork() cualquier texto que el padre ya haya impreso pero no haya 
volcado todavía queda copiado en la imagen de memoria del hijo y termina imprimiéndose dos veces, una cuando el padre lo vuelca y otra cuando el hijo termina, en `activity.cpp` el hijo siempre sale con `_exit()`, nunca `exit()`, 
precisamente para no volcar ese buffer heredado.

### 6. Manejador de SIGINT sencillo
Dentro de un manejador de señal solo es seguro llamar funciones *async-signal-safe*. Por eso manejador_sigint() no hace nada más que poner sigintRecibido = 1, toda la lógica real como mandar SIGTERM a los hijos, esperarlos con 
`waitpid()`, imprimir mensajes esta en apagar_por_sigint() que corre en el ciclo principal del planificador, no dentro del manejador. El hijo por su parte, ignora SIGINT para que el apagado sea siempre decidido y 
coordinado por el padre.


