/*------------------------------------------------------------------------*/
/*  HOFlow - Higher Order Flow                                            */
/*  CFD Solver based ond CVFEM                                            */
/*------------------------------------------------------------------------*/
#ifndef SIMULATION_H
#define SIMULATION_H

#include <yaml-cpp/yaml.h>

class Realm;
class LinearSolvers;

class Simulation {
public:
    const YAML::Node& m_root_node;
    Realm *realm_;
    LinearSolvers *linearSolvers_;
    
    Simulation(const YAML::Node& root_node);
    ~Simulation();
    void load(const YAML::Node& node);
    void initialize();
    void run();
};

#endif /* SIMULATION_H */

