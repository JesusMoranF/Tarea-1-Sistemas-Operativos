#ifndef ACTIVITY_HPP
#define ACTIVITY_HPP
#include "dag.hpp"

// codigo que corre dentro del proceso hijo tras el fork para una actividad, siempre termina con exit

// fdEntrada: extremo de lectura del pipe padre al hijo, el hijo le eaqui los mensajes de las actividades de las que depende que el padre ya recolecto y se las envia

// fdSalida:  extremo de escritura del pipe hijo al padre, el hijo escribe aqui su resultado OK|... o FAIL|... para que el padre lo mande a los dependientes

// porcentajeFallo: probabilidad 0-100 de fallo simulado, solo para poder demostrar el aislamiento de errores ya que no fallaria realmente


void activity_run_child(const Activity &actividades, int fdEntrada, int fdSalida, int porcentajeFallo);

#endif 
