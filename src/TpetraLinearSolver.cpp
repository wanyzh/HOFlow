/*------------------------------------------------------------------------*/
/*  HOFlow - Higher Order Flow                                            */
/*  CFD Solver based ond CVFEM                                            */
/*------------------------------------------------------------------------*/
#include "TpetraLinearSolver.h"

#include <HOFlowEnv.h>
#include <TpetraLinearSolverConfig.h>

#include <stk_util/util/ReportHandler.hpp>

// Tpetra support
#include <BelosLinearProblem.hpp>
#include <BelosMultiVecTraits.hpp>
#include <BelosOperatorTraits.hpp>
#include <BelosSolverFactory.hpp>
#include <BelosSolverManager.hpp>
#include <BelosConfigDefs.hpp>
#include <BelosLinearProblem.hpp>
#include <BelosTpetraAdapter.hpp>

#include <Ifpack2_Factory.hpp>
#include <Kokkos_DefaultNode.hpp>
#include <Kokkos_Serial.hpp>
#include <Teuchos_ArrayRCP.hpp>
#include <Teuchos_DefaultMpiComm.hpp>
#include <Teuchos_OrdinalTraits.hpp>
#include <Tpetra_CrsGraph.hpp>
#include <Tpetra_Export.hpp>
#include <Tpetra_Operator.hpp>
#include <Tpetra_Map.hpp>
#include <Tpetra_MultiVector.hpp>
#include <Tpetra_Vector.hpp>

#include <Teuchos_ParameterXMLFileReader.hpp>

#include <iostream>

TpetraLinearSolver::TpetraLinearSolver(std::string solverName,
                                        TpetraLinearSolverConfig *config,
                                        const Teuchos::RCP<Teuchos::ParameterList> params,
                                        const Teuchos::RCP<Teuchos::ParameterList> paramsPrecond,
                                        LinearSolvers *linearSolvers) :
    LinearSolver(solverName,linearSolvers, config),
    params_(params),
    paramsPrecond_(paramsPrecond),
    preconditionerType_(config->preconditioner_type())
{
    // nothing to do
}

TpetraLinearSolver::~TpetraLinearSolver() {
    destroyLinearSolver();
}

void TpetraLinearSolver::setSystemObjects(Teuchos::RCP<LinSys::Matrix> matrix, Teuchos::RCP<LinSys::Vector> rhs) {
    ThrowRequire(!matrix.is_null());
    ThrowRequire(!rhs.is_null());

    matrix_ = matrix;
    rhs_ = rhs;
}

void TpetraLinearSolver::setupLinearSolver(Teuchos::RCP<LinSys::Vector> sln,
                                            Teuchos::RCP<LinSys::Matrix> matrix,
                                            Teuchos::RCP<LinSys::Vector> rhs,
                                            Teuchos::RCP<LinSys::MultiVector> coords) {
    setSystemObjects(matrix,rhs);
    problem_ = Teuchos::RCP<LinSys::LinearProblem>(new LinSys::LinearProblem(matrix_, sln, rhs_) );

    Ifpack2::Factory factory;
    preconditioner_ = factory.create(preconditionerType_, 
                                      Teuchos::rcp_const_cast<const LinSys::Matrix>(matrix_), 0);
    preconditioner_->setParameters(*paramsPrecond_);

    // delay initialization for some preconditioners
    if ( "RILUK" != preconditionerType_ ) {
        preconditioner_->initialize();
    }
    problem_->setRightPrec(preconditioner_);

    // create the solver, e.g., gmres, cg, tfqmr, bicgstab
    LinSys::SolverFactory sFactory;
    solver_ = sFactory.create(config_->get_method(), params_);
    solver_->setProblem(problem_);
}

void TpetraLinearSolver::destroyLinearSolver() {
    problem_ = Teuchos::null;
    preconditioner_ = Teuchos::null;
    solver_ = Teuchos::null;
    coords_ = Teuchos::null;
}

int TpetraLinearSolver::residual_norm(int whichNorm, Teuchos::RCP<LinSys::Vector> sln, double& norm) {
    LinSys::Vector resid(rhs_->getMap());
    ThrowRequire(! (sln.is_null()  || rhs_.is_null() ) );

    if (matrix_->isFillActive() )
    {
        // FIXME
        //!matrix_->fillComplete(map_, map_);
        throw std::runtime_error("residual_norm");
    }
    matrix_->apply(*sln, resid);

    resid.update(-1.0, *rhs_, 1.0); 

    if ( whichNorm == 0 )
        norm = resid.normInf();
    else if ( whichNorm == 1 )
        norm = resid.norm1();
    else if ( whichNorm == 2 )
        norm = resid.norm2();
    else
        return 1;

    return 0;
}

int TpetraLinearSolver::solve(Teuchos::RCP<LinSys::Vector> sln, int & iters, double & finalResidNrm, bool isFinalOuterIter) {
    ThrowRequire(!sln.is_null());

    const int status = 0;
    int whichNorm = 2;
    finalResidNrm=0.0;

    double time = -HOFlowEnv::self().hoflow_time();
    if ( "RILUK" == preconditionerType_ ) {
      preconditioner_->initialize();
    }
    preconditioner_->compute();
    time += HOFlowEnv::self().hoflow_time();

    // Update preconditioner timer for this timestep; actual summing over
    // timesteps is handled in EquationSystem::assemble_and_solve
    timerPrecond_ = time;

    Teuchos::RCP<Teuchos::ParameterList> params(Teuchos::rcp(new Teuchos::ParameterList));
    if (isFinalOuterIter) {
        params->set("Convergence Tolerance", config_->finalTolerance());
    } else {
        params->set("Convergence Tolerance", config_->tolerance());
    }

    solver_->setParameters(params);

    problem_->setProblem();
    solver_->solve();

    iters = solver_->getNumIters();
    residual_norm(whichNorm, sln, finalResidNrm);

    return status;
}

