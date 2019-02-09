/*------------------------------------------------------------------------*/
/*  HOFlow - Higher Order Flow                                            */
/*  CFD Solver based ond CVFEM                                            */
/*------------------------------------------------------------------------*/
#ifndef LINEARSOLVER_H
#define LINEARSOLVER_H

#include <TpetraLinearSolver.h>

//! Stores a linear solver specified in the input file
class LinearSolver {
public:
    LinearSolver();
    ~LinearSolver();
};

#endif /* LINEARSOLVER_H */

