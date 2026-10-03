#include "scheduler.hpp"
#include "activity.hpp"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <csignal>
#include <cerrno>
#include <sys/wait.h>
#include <ctime>
#include <string>

using namespace std;

volatile sig_atomic_t sigintRecibido = 0;

extern "C" void manejador_sigint(int){
    sigintRecibido = 1;
}

// lanza la actividad como proceso hijo, luego crea los dos pipes de entrada y salida, hace fork y en el padre deja todo listo para que el loop principal pueda esperar y leer el resultado después, retorna el pid del hijo o -1 si algo fallo.
pid_t lanzar_actividad(Dag &dag, int idx, SchedulerConfig cfg){
    Activity &a = dag.actividades[idx];

    int pipeEntrada[2];
    int pipeSalida[2];
    if (pipe(pipeEntrada) == -1){
        perror("[scheduler] pipe");
        return -1;
    }
    if (pipe(pipeSalida) == -1){
        perror("[scheduler] pipe");
        return -1;
    }

    // hay que vaciar el buffer de stdout antes del fork
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) {
      perror("[scheduler] fork");
        return -1;
    }

    if (pid == 0){
        // proceso hijo
        close(pipeEntrada[1]);
        close(pipeSalida[0]);
        ejecutar_actividad(a, pipeEntrada[0], pipeSalida[1], cfg.porcentajeFallo);
        _exit(1); 
    }

    //  proceso padre
    close(pipeEntrada[0]);
    close(pipeSalida[1]);
    close(pipeEntrada[1]);

    if (a.msgSalida.size() > 0){
        ssize_t w = write(pipeEntrada[1], a.msgSalida.data(), a.msgSalida.size());
        (void)w; // si falla el hijo simplemente no recibe insumo
   }

    a.pid = pid;
    a.fd_salida = pipeSalida[0];
    a.estado = RUNNING;
    printf("[Planificador] -> Actividad %s ('%s') iniciada, pid=%d, duracion=%ldms\n", a.id.c_str(), a.nombre.c_str(), static_cast<int>(pid), a.tiempo_ms);
    fflush(stdout);
    return pid;
}

// recorre los dependientes de una actividad que fallo y los marca como ABORTED
void abortar_dependientes(Dag &dag, int idx, int *contador){
    Activity &a = dag.actividades[idx];

    for (int k = 0; k < (int)a.sucesores.size(); k++){
        int v = a.sucesores[k];
        Activity &av = dag.actividades[v];

        if (av.estado == PENDING){
            av.estado = ABORTED;
            *contador = *contador + 1;
            printf("[Planificador] Actividad %s ('%s') ABORTADA (depende de una rama fallida)\n", av.id.c_str(), av.nombre.c_str());
            abortar_dependientes(dag, v, contador);
        }else if (av.estado == RUNNING){
            av.estado = ABORTED;
            *contador = *contador + 1;
            if (av.pid > 0){
                kill(av.pid, SIGTERM);
            }
            printf("[Planificador] Actividad %s ('%s') ABORTADA en pleno vuelo (depende de una rama fallida)\n", av.id.c_str(), av.nombre.c_str());
            abortar_dependientes(dag, v, contador);
        }
    }
}

// al terminar con exito una actividad revisa sus dependientes
void marcar_exito(Dag &dag, int idx, const string &detalle){
    Activity &a = dag.actividades[idx];

    for (int k = 0; k < (int)a.sucesores.size(); k++){
        int v = a.sucesores[k];
        Activity &av = dag.actividades[v];

        if (av.state != PENDING){
            continue; // ya fallo o ya fue abortada
      }

        if (av.msgSalida.size() > 0){
            av.msgSalida = av.msgSalida + "; ";
        }
        av.msgSalida = av.msgSalida + a.id + ":" + detalle;

        a.dep_pendientes = a.dep_pendientes - 1;
     }
}



    return true;
}
