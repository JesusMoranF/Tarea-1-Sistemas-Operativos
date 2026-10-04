#include <iostream> 
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include "dag.hpp"
#include "scheduler.hpp"

using namespace std;

void print_usage(const char *prog){
    cerr << "Uso: " << prog << " plan.txt K [porcentajeFallo]\n" << "  plan.txt, archivo con las actividades.  K, maximo de procesos concurrentes. porcentajeFallo, probabilidad de fallo simulado por actividad";
}

int main(int argc, char *argv[]){

 //aca el arreglo significa para argv[0] el nombre del programa, para argv[1] es el plan_path (ubicación del .txt), para argv[2] es el límite de concurrencia K y opcionalmente argv[3] el porcentaje de fallo simulado
    
    if (argc < 3){
        print_usage(argv[0]);
        return 1;
    }

    string plan_path = argv[1];
    int k = atoi(argv[2]); //para pasarlo a int
    if (k < 1){
        cerr<<"Error: K debe ser un entero >= 1\n";
        return 1;
    }

    SchedulerConfig cfg;
    cfg.porcentajeFallo = 0;
    if (argc >= 4){
        cfg.porcentajeFallo = atoi(argv[3]);
        if (cfg.porcentajeFallo < 0){
            cfg.porcentajeFallo = 0;
        }
        if (cfg.porcentajeFallo > 100){
            cfg.porcentajeFallo = 100;
        }
    }

    srand(static_cast<unsigned int>(time(nullptr) ^ getpid()));

    Dag dag;
    if (!dag_load(dag, plan_path)){
        cerr<<"Error cargando'"<<plan_path<<"'\n";
        return 1;
    }

    cout << "Planificador. Cargadas "<<dag.actividades.size()<<" actividades desde '"<< plan_path << "', K=" << k;
    if (cfg.porcentajeFallo > 0){
        cout<<"(fallo simulado="<<cfg.porcentajeFallo<<"%)";
    }
    cout<<endl;
    cout.flush();

    bool ok = scheduler_run(dag, k, cfg);

    if (ok){
        return 0;
    }

    return 1;
}
