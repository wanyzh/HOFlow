/*------------------------------------------------------------------------*/
/*  HOFlow - Higher Order Flow                                            */
/*  CFD Solver based ond CVFEM                                            */
/*------------------------------------------------------------------------*/
#ifndef BOUNDARYCONDITION_H
#define BOUNDARYCONDITION_H

#include <yaml-cpp/yaml.h>
#include <Enums.h>
#include <string>

class BoundaryConditions;
class YAML::Node;
class Simulation;

class BoundaryCondition {
public:
    BoundaryCondition(BoundaryConditions & bcs);
    virtual ~BoundaryCondition();
    BoundaryCondition * load(const YAML::Node & node);
    Simulation * root();
    BoundaryConditions * parent();
    
    std::string bcName_;
    std::string targetName_;
    BoundaryConditionType theBcType_;
    BoundaryConditions & boundaryConditions_;
};

#endif /* BOUNDARYCONDITION_H */

