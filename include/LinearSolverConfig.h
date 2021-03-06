/*------------------------------------------------------------------------*/
/*  HOFlow - Higher Order Flow                                            */
/*  CFD Solver based ond CVFEM                                            */
/*------------------------------------------------------------------------*/
#ifndef LINEARSOLVERCONFIG_H
#define LINEARSOLVERCONFIG_H

#include <string>
#include <Teuchos_ParameterList.hpp>
#include <Teuchos_RCP.hpp>
#include <yaml-cpp/yaml.h>

class YAML::Node;

//! Configuration of a linear solver
class LinearSolverConfig {
public:
    LinearSolverConfig();
    virtual ~LinearSolverConfig() = default;
    virtual void load(const YAML::Node &) = 0;
    
    inline std::string name() const { 
        return name_ ; 
    }

    const Teuchos::RCP<Teuchos::ParameterList> & params() const { 
        return params_; 
    }

    const Teuchos::RCP<Teuchos::ParameterList> & paramsPrecond() const { 
        return paramsPrecond_; 
    }

    inline bool getWriteMatrixFiles() const { 
        return writeMatrixFiles_; 
    }

    inline bool recomputePreconditioner() const { 
        return recomputePreconditioner_; 
    }

    inline bool reusePreconditioner() const { 
        return reusePreconditioner_; 
    }

    std::string get_method() const {
        return method_;
    }

    std::string preconditioner_type() const { 
        return preconditionerType_;
    }

    inline double tolerance() const { 
        return tolerance_; 
    }
    
    inline double finalTolerance() const { 
        return finalTolerance_; 
    }

    std::string solver_type() const { 
        return solverType_; 
    }
    
protected:
    std::string solverType_;
    std::string name_;
    std::string method_;
    std::string precond_;
    std::string preconditionerType_{"RELAXATION"};
    double tolerance_;
    double finalTolerance_;


    Teuchos::RCP<Teuchos::ParameterList> params_;
    Teuchos::RCP<Teuchos::ParameterList> paramsPrecond_;

    bool recomputePreconditioner_{true};
    bool reusePreconditioner_{false};
    bool writeMatrixFiles_{false};
};

#endif /* LINEARSOLVERCONFIG_H */

