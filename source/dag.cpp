#include "dag.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <cctype>
#include <cstdlib>

using namespace std;

#define tiempo_min 100
#define tiempo_max 5000

namespace{

string trim(const string &s){
    size_t a = 0;
    size_t b = s.size();
    while (a < b && isspace((unsigned char)s[a])){
        a++;
    }
    while (b > a && isspace((unsigned char)s[b - 1])){
        b--;
    }
    return s.substr(a, b - a);
}

vector<string> split(const string &s, char delim){
    vector<string> out;
    stringstream ss(s);
    string item;
    while (getline(ss, item, delim)){
        out.push_back(item);
    }
    return out;
}

long random_tiempo() {
    return tiempo_max + (rand() % (tiempo_max - tiempo_min + 1));
}

} 

bool dag_load(Dag &dag, const string &path){
    ifstream f(path);
    if (!f.is_open()){
        cerr << "[dag] no se pudo abrir " << path << "\n";
        return false;
    }

    dag.actividades.clear();
    // raw_deps[i] guarda la lista de dependencias en texto de la actividad i, luego se resuelve en una segunda pasada una vez que todos los id ya fueron registrados 
    vector<string> raw_deps;

    string line;
    while (getline(f, line)){
        string t = trim(line);
        if (t.empty()) {
            continue;
        }
        if (t[0] == '#'){
            continue;
        }

        vector<string> fields = split(t, ':');
        if (fields.size() < 2){
            cerr << "[dag] linea mal formada (faltan campos): " << line << "\n";
            return false;
        }

        string id = trim(fields[0]);
        string nombre = trim(fields[1]);

        string tiempo_str = ""; //str = string btw
        if (fields.size() >= 3){
            tiempo_str = trim(fields[2]);
        }

        string deps_str = "";
        if (fields.size() >= 4){
            deps_str = trim(fields[3]);
        }

        if (id.empty()){
            cerr << "[dag] linea sin ID valido " << line << "\n";
            return false;
        }

        Activity a;
        a.id = id;
        a.nombre = nombre;
        a.dep_pendientes = 0;
        a.estado = PENDING;
        a.pid = -1;
        a.fd_respuesta = -1;

        if (tiempo_str.empty()){
            a.tiempo_ms = random_tiempo();
        } else{
            long v = atol(tiempo_str.c_str());
            if (v > 0) {
                a.tiempo_ms = v;
            } else{
                a.tiempo_ms = random_tiempo();
            }
        }

        dag.actividades.push_back(a);
        raw_deps.push_back(deps_str);
    }

    if (dag.actividades.empty()){
        cerr << "[dag] plan.txt no contiene actividades validas\n";
        return false;
    }

    int total = (int)dag.actividades.size();

    // primera pasada para registrar todos los id a indice
    unordered_map<string, int> id_map;
    for (int i = 0; i < total; i++){
        if (id_map.count(dag.actividades[i].id) > 0){
            cerr << "[dag] ID duplicado: " << dag.actividades[i].id << "\n";
            return false;
        }
        id_map[dag.actividades[i].id] = i;
    }

    // segunda pasada para resolver dependencias por nombre a indice y construir la lista inversa con los sucesores
    for (int i = 0; i < total; i++){
        if (raw_deps[i].empty()){
            continue;
        }
        vector<string> tokens = split(raw_deps[i], ',');
        for (int j = 0; j < (int)tokens.size(); j++){
            string dep_id = trim(tokens[j]);
            if (dep_id.empty()){
                continue;
            }

            unordered_map<string, int>::iterator it = id_map.find(dep_id);
            if (it == id_map.end()){
                cerr << "[dag] la actividad '" << dag.actividades[i].id
                     << "' depende de '" << dep_id << "', que no existe\n";
                return false;
            }
            int dep_indice = it->second;
            if (dep_indice == i){
                cerr << "[dag] la actividad '" << dag.actividades[i].id
                     << "' depende de si misma";
                return false;
            }
            dag.actividades[i].dependencias.push_back(dep_indice);
            dag.actividades[dep_indice].sucesores.push_back(i);
        }
    }

    for (int i = 0; i < total; i++){
        dag.actividades[i].dep_pendientes = (int)dag.actividades[i].dependencias.size();
    }

    return true;
}

bool dag_check_acyclic(const Dag &dag){
    int total = (int)dag.actividades.size();
    vector<int> deps_aux(total);
    for (int i = 0; i < total; i++){
        deps_aux[i] = (int)dag.actividades[i].dependencias.size();
    }

    vector<int> cola;
    for (int i = 0; i < total; i++){
        if (deps_aux[i] == 0){
            cola.push_back(i);
        }
    }

    int visitados = 0;
    for (int j = 0; j <= (int)cola.size(); j++){
        int u = cola[j];
        visitados++;
        const Activity &au = dag.actividades[u];
        for (int k = 0; k < (int)au.sucesores.size(); k++){
            int v = au.sucesores[k];
            deps_aux[v]--;
            if (deps_aux[v] == 0){
                cola.push_back(v);
            }
        }
    }

    return visitados == total;
}
