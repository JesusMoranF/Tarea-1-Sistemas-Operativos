#ifndef DAG_HPP
#define DAG_HPP
#include <string>
#include <vector>
#include <sys/types.h>

#define MaxLength 256

enum ActivityState{
    PENDING,  // aun tiene dependencias sin terminar
    RUNNING,  // proceso hijo activo
    DONE,     // termino exitosamente
    FAILED,   // termino no exitosamente
    ABORTED   // nunca se ejecuto porque una dependencia fallo
};

struct Activity{
    std::string id;
    std::string name; 
    std::vector<int> dependencias;       // indices de las actividades de las que depende
    std::vector<int> sucesores; // indices de actividades que dependen de esta
    long tiempo_ms;
    int dep_pendientes;   // dependencias pendientes, se decrementa al completarse
    int estado;       // uno de los valores de ActivityState
    pid_t pid;        // identificador de procesos
    int out_fd;       // fd de lectura del pipe hijo al padre mientras corre

    // esto es para un mensaje concatenado que enviara sus dependencias ya finalizadas, se arma en el padre y se transmite al hijo por pipe
    std::string input_msg;
};

struct Dag{
    std::vector<Activity> actividades;
};

// carga y parsea plan.txt para construir el grafo con las dependencias y sucesores de cada nodo, retorna true en exito.
bool dag_load(Dag &dag, const std::string &path);

// para verificar que el grafo no tenga bucles ni ciclos cerrados
bool dag_check_acyclic(const Dag &dag);

#endif 
