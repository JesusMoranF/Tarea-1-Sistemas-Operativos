#include "activity.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <csignal>
#include <ctime>
#include <cerrno>

using namespace std;

#define tamano_max 4096

// se guarda el fd de salida en una variable global para poder usarlo desde el manejador de SIGTERM ya que el manejador no recibe argumentos propios
volatile sig_atomic_t fdSalidaGlobal = -1;
char msgAborto[128];
volatile size_t lenAborto = 0;

extern "C" void manejador_sigterm(int){
    if (fdSalidaGlobal >= 0 && lenAborto > 0){
        ssize_t w = write(static_cast<int>(fdSalidaGlobal), msgAborto, lenAborto);
        (void)w; // si falla igual sale
    }
    _exit(143);
}

// lee todo lo disponible en el pipe hasta el final de archivo, el padre cierra su extremo de escritura apenas termina de enviar el mensaje y asi el hijo sabe cuando dejar de esperar
void leer_mensaje(int fd, char *buf, size_t tam){
    size_t total = 0;
    buf[0] = '\0';
    while (total + 1 < tam){
        ssize_t r = read(fd, buf + total, tam - 1 - total);
        if (r <= 0){
            break; // si es 0 es porque es el fin del archivo y si es <0 entonces es un error, en ambos casos se debe parar asique todos los caminos llevan al break
        }
        total += static_cast<size_t>(r);
    }
    buf[total] = '\0';
}

void write_all(int fd, const char *buf, size_t len){
    size_t sent = 0;
    while (sent < len){
        ssize_t w = write(fd, buf + sent, len - sent);
        if (w <= 0){
            if (errno == EINTR){
                continue;
            }
            break;
        }
        sent += static_cast<size_t>(w);
    }
}

void ejecutar_actividad(const Activity &actividades, int fdEntrada, int fdSalida, int porcentajeFallo){

    // el SIGINT lo maneja unicamente el padre, que decide de forma ordenada abortar todo y el hijo lo ignora para no duplicar logicá
    signal(SIGINT, SIG_IGN);

    // se prepara el mensaje de aborto por si llega un SIGTERM del padre mientras esta corriendo
    fdSalidaGlobal = fdSalida;
    int largoAborto = snprintf(msgAborto, sizeof(msgAborto), "ABORT|%s|%s|interrumpida por senal", actividades.id.c_str(), actividades.nombre.c_str());
    if (largoAborto > 0){
        lenAborto = static_cast<size_t>(largoAborto);
    }else{
        lenAborto = 0;
    }
    signal(SIGTERM, manejador_sigterm);

    // leer insumo de las dependencias que mando el padre
    char msgEntrada[tamano_max];
    leer_mensaje(fdEntrada, msgEntrada, sizeof(msgEntrada));
    close(fdEntrada);

    if (strlen(msgEntrada) > 0){
        printf("[Actividad %s] '%s' recibio insumo: %s\n", actividades.id.c_str(), actividades.nombre.c_str(), msgEntrada);
    } else{
        printf("[Actividad %s] '%s' no tiene dependencias, comienza de inmediato\n", actividades.id.c_str(), actividades.nombre.c_str());
    }
    fflush(stdout);

    // esto es para que la semilla por proceso getpid y time sea distinta, de esa forma no se repite la misma secuencia aleatoria entre helmanos que hicieron fork casi al mismo tiempo
    srand(static_cast<unsigned int>(time(nullptr) ^ getpid()));

    // se usa nanosleep en vez de un loop de polling
    struct timespec tiempo;
    tiempo.tv_sec = actividades.tiempo_ms / 1000;
    tiempo.tv_nsec = (actividades.tiempo_ms % 1000) * 1000000L;
    while (nanosleep(&tiempo, &tiempo) == -1 && errno == EINTR){
        continue; // reintentar con el tiempo que queda
    }

    bool fallo = false;
    if (porcentajeFallo > 0){
        int roll = rand() % 100;
        if (roll < porcentajeFallo){
            fallo = true;
        }
    }

    char msgSalida[tamano_max];
    if (fallo){
        int largoFallo = snprintf(msgSalida, sizeof(msgSalida), "FAIL|%s|%s|error simulado durante la ejecucion", actividades.id.c_str(), actividades.nombre.c_str());
        write_all(fdSalida, msgSalida, static_cast<size_t>(largoFallo));
        close(fdSalida);
        printf("[Actividad %s] '%s' FALLO\n", actividades.id.c_str(), actividades.nombre.c_str());
        fflush(stdout);
        _exit(1);
    }                      //por si falla o no falla

    int largoOk = snprintf(msgSalida, sizeof(msgSalida), "OK|%s|%s|listo (%ldms)", actividades.id.c_str(), actividades.nombre.c_str(), actividades.tiempo_ms);
    write_all(fdSalida, msgSalida, static_cast<size_t>(largoOk));
    close(fdSalida);
    printf("[Actividad %s] '%s' completada exitosamente\n", actividades.id.c_str(), actividades.nombre.c_str());
    fflush(stdout);
    _exit(0);
}
