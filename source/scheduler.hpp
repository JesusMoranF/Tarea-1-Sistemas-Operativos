#ifndef SCHEDULER_HPP
#define SCHEDULER_HPP
#include "dag.hpp"

struct SchedulerConfig{
    int porcentajeFallo; 
};

// ejecuta el dag completo respetando el limite de concurrencia k, retorna true si todo termino con o sin actividades fallidas, o false si hubo un error irrecuperable como el del verificar si el grafo es aciclico
bool scheduler_run(Dag &dag, int k, SchedulerConfig cfg);


#endif 
