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

#define tamano_max 4096

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
    
  if (a.msgSalida.size() > 0){
        ssize_t w = write(pipeEntrada[1], a.msgSalida.data(), a.msgSalida.size());
        (void)w; // si falla el hijo simplemente no recibe insumo
   }
    
    close(pipeEntrada[1]);

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
            printf("[Planificador] Actividad %s ('%s') abortada\n", av.id.c_str(), av.nombre.c_str());
            abortar_dependientes(dag, v, contador);
        }else if (av.estado == RUNNING){
            av.estado = ABORTED;
            *contador = *contador + 1;
            if (av.pid > 0){
                kill(av.pid, SIGTERM);
            }
            printf("[Planificador] Actividad %s ('%s') abortada en plena run\n", av.id.c_str(), av.nombre.c_str());
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

        if (av.estado != PENDING){
            continue; // ya fallo o ya fue abortada
      }

        if (av.msgSalida.size() > 0){
            av.msgSalida = av.msgSalida + "; ";
        }
        av.msgSalida = av.msgSalida + a.id + ":" + detalle;

        a.dep_pendientes = a.dep_pendientes - 1;
     }
}

// apagado ante SIGINT
void apagar_por_sigint(Dag &dag){
    printf("\n[Planificador] *** SIGINT recibido: Inspección de la Seremi "
           "Cancelen todo... ***\n");
    fflush(stdout);

    int total = (int)dag.actividades.size();

    for (int i = 0; i < total; i++){
        if (dag.actividades[i].estado == RUNNING){
            kill(dag.actividades[i].pid, SIGTERM);
        }
    }

    for (int i = 0; i < total; i++){
        if (dag.actividades[i].estado == RUNNING){
            int status;
            waitpid(dag.actividades[i].pid, &status, 0);
            close(dag.actividades[i].fd_salida);
            dag.actividades[i].fd_salida = -1;
            dag.actividades[i].estado = ABORTED;
            printf("[Planificador] Actividad %s ('%s') detenida por SIGINT\n", dag.actividades[i].id.c_str(), dag.actividades[i].nombre.c_str());
        }
    }

    for (int i = 0; i < total; i++){
        if (dag.actividades[i].estado == PENDING){
            dag.actividades[i].estado = ABORTED;
        }
    }

    fflush(stdout);
}

bool scheduler_run(Dag &dag, int k, SchedulerConfig cfg){
    if (!es_aciclico(dag)){
        fprintf(stderr, "[scheduler] Error: el grafo de dependencias tiene un ciclo barra bucle.\n");
        return false;
    }
    if (k < 1){
        k = 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = manejador_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    int total = (int)dag.actividades.size();
    int activos = 0;
    int completadas = 0;
    int fallidas = 0;
    int abortadas = 0;

    struct timespec tiempoInicio;
    struct timespec tiempoFin;
    clock_gettime(CLOCK_MONOTONIC, &tiempoInicio);

    while (true){
        if (sigintRecibido){
            apagar_por_sigint(dag);

            completadas = 0;
            fallidas = 0;
            abortadas = 0;
            for (int i = 0; i < total; i++){
                if (dag.actividades[i].estado == DONE){
                    completadas++;
                } else if (dag.actividades[i].estado == FAILED){
                    fallidas++;
                }else if (dag.actividades[i].estado == ABORTED){
                    abortadas++;
                }
            }
            printf("\n[Planificador] Ejecucion interrumpida. "
                   "Completadas=%d Fallidas=%d Abortadas=%d\n", completadas, fallidas, abortadas);
            return true;
        }

        for (int i = 0; i < total && activos < k; i++){
            if (dag.actividades[i].estado == PENDING && dag.actividades[i].dep_pendientes == 0){
                pid_t pid = lanzar_actividad(dag, i, cfg);
                if (pid > 0){
                    activos++;
                }else{
                    dag.actividades[i].estado = FAILED;
                    fallidas++;
                    abortar_dependientes(dag, i, &abortadas);
            }
            }
        }

        if (activos == 0){
            break;
        }

        int status;
        pid_t pid = waitpid(-1, &status, 0);
        if (pid == -1){
            if (errno == EINTR){
                continue;
            }
            perror("[scheduler] waitpid");
            break;
        }

        int idx = -1;
        for (int i = 0; i < total; i++){
            if (dag.actividades[i].pid == pid){
                idx = i;
                break;
            }
        }
        if (idx == -1){
            continue;
        }

        activos--;

        if (dag.actividades[idx].estado == ABORTED){
            close(dag.actividades[idx].fd_salida);
            dag.actividades[idx].fd_salida = -1;
            continue;
        }

        char mensaje[tamano_max];
        mensaje[0] = '\0';
        ssize_t r = read(dag.actividades[idx].fd_salida, mensaje, sizeof(mensaje) - 1);
        if (r > 0){
            mensaje[r] = '\0';
        }
        close(dag.actividades[idx].fd_salida);
        dag.actividades[idx].fd_salida = -1;
        string mensajeResultado(mensaje);

        bool salioOk = false;
        if (WIFEXITED(status) && status == 0){
            salioOk = true;
        }

        string detalle = mensajeResultado;
        size_t p1 = mensajeResultado.find('|');
        if (p1 != string::npos){
            size_t p2 = mensajeResultado.find('|', p1 + 1);
            if (p2 != string::npos){
                size_t p3 = mensajeResultado.find('|', p2 + 1);
                if (p3 != string::npos){
                    detalle = mensajeResultado.substr(p3 + 1);
                }
            }
        }

        if (salioOk && mensajeResultado.rfind("FAIL", 0) != 0){
            dag.actividades[idx].estado = DONE;
            completadas++;
            printf("[Planificador] <- Actividad %s ('%s') completada: %s\n", dag.actividades[idx].id.c_str(), dag.actividades[idx].nombre.c_str(), detalle.c_str());
            marcar_exito(dag, idx, detalle);
        } else{
            dag.actividades[idx].estado = FAILED;
            fallidas++;
            string mostrar = detalle;
            if (mostrar.empty()){
                mostrar = "(sin detalle, proceso murio)";
            }
            printf("[Planificador] <- Actividad %s ('%s') fallo: %s\n", dag.actividades[idx].id.c_str(), dag.actividades[idx].nombre.c_str(), mostrar.c_str());
            abortar_dependientes(dag, idx, &abortadas);
        }
        fflush(stdout);
    }

    clock_gettime(CLOCK_MONOTONIC, &tiempoFin);
    double elapsed = (tiempoFin.tv_sec - tiempoInicio.tv_sec) + (tiempoFin.tv_nsec - tiempoInicio.tv_nsec) / 1e9; // esto convierte los nanosegundos en una fracción de segundo

    printf("\n     Resúmen de la simulación    \n");
    printf("Total actividades : %d\n", total);
    printf("Completadas       : %d\n", completadas);
    printf("Fallidas          : %d\n", fallidas);
    printf("Abortadas         : %d\n", abortadas);
    printf("Tiempo total real : %.3f s\n", elapsed);

    return true;
}
