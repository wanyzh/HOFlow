/*------------------------------------------------------------------------*/
/*  HOFlow - Higher Order Flow                                            */
/*  CFD Solver based ond CVFEM                                            */
/*------------------------------------------------------------------------*/
#include "HeatCondEquationSystem.h"

#include "AssembleElemSolverAlgorithm.h"
//#include "AssembleHeatCondWallSolverAlgorithm.h"
//#include "AssembleHeatCondIrradWallSolverAlgorithm.h"
//#include "AssembleScalarEdgeDiffSolverAlgorithm.h"
#include "AssembleScalarElemDiffSolverAlgorithm.h"
//#include "AssembleScalarDiffNonConformalSolverAlgorithm.h"
#include "AssembleScalarFluxBCSolverAlgorithm.h"
#include "AssembleScalarFluxBCSolverAlgorithmNalu.h"
#include "AssembleNodalGradAlgorithmDriver.h"
//#include "AssembleNodalGradEdgeAlgorithm.h"
#include "AssembleNodalGradElemAlgorithm.h"
#include "AssembleNodalGradBoundaryAlgorithm.h"
//#include "AssembleNodalGradNonConformalAlgorithm.h"
#include "AssembleNodeSolverAlgorithm.h"
#include "AuxFunctionAlgorithm.h"
#include "ConstantBCAuxFunctionAlgorithm.h"
#include "ConstantAuxFunction.h"
#include "CopyFieldAlgorithm.h"
#include "DirichletBC.h"
#include <AssembleScalarDirichletBCSolverAlgorithm.h>
#include "EquationSystem.h"
#include "EquationSystems.h"
#include "Enums.h"
//#include "ErrorIndicatorAlgorithmDriver.h"
#include <FieldFunctions.h>
#include <FieldTypeDef.h>
#include "LinearSolvers.h"
#include "LinearSolver.h"
#include "LinearSystem.h"
#include "master_element/MasterElement.h"
#include "HOFlowEnv.h"
#include "Realm.h"
#include "Realms.h"
#include "HeatCondMassBackwardEulerNodeSuppAlg.h"
#include "HeatCondMassBDF2NodeSuppAlg.h"
#include "ProjectedNodalGradientEquationSystem.h"
//#include "PstabErrorIndicatorEdgeAlgorithm.h"
//#include "PstabErrorIndicatorElemAlgorithm.h"
//#include "SimpleErrorIndicatorScalarElemAlgorithm.h"
#include "Simulation.h"
#include "SolutionOptions.h"
#include "TimeIntegrator.h"
#include "SolverAlgorithmDriver.h"

// template for kernels
#include "AlgTraits.h"
#include "kernel/KernelBuilder.h"
#include "kernel/KernelBuilderLog.h"

// HOFlow utils
#include "utils/StkHelpers.h"

// stk_util
#include <stk_util/parallel/Parallel.hpp>

// stk_mesh/base/fem
#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/Field.hpp>
#include <stk_mesh/base/FieldParallel.hpp>
#include <stk_mesh/base/GetBuckets.hpp>
#include <stk_mesh/base/GetEntities.hpp>
#include <stk_mesh/base/CoordinateSystems.hpp>
#include <stk_mesh/base/MetaData.hpp>
#include <stk_mesh/base/SkinMesh.hpp>
#include <stk_mesh/base/Comm.hpp>
#include <stk_mesh/base/Part.hpp>
#include <stk_mesh/base/Selector.hpp>

// stk_io
#include <stk_io/IossBridge.hpp>
#include <stk_topology/topology.hpp>

// stk_util
#include <stk_util/parallel/ParallelReduce.hpp>

HeatCondEquationSystem::HeatCondEquationSystem(EquationSystems & eqSystems) :
    EquationSystem(eqSystems, "HeatCondEQS", "temperature"),
    managePNG_(realm_.get_consistent_mass_matrix_png("temperature")),
    temperature_(NULL),
    dtdx_(NULL),
    tTmp_(NULL),
    dualNodalVolume_(NULL),
    coordinates_(NULL),
    exact_temperature_(NULL),
    exact_dtdx_(NULL),
    exact_laplacian_(NULL),
    density_(NULL),
    specHeat_(NULL),
    thermalCond_(NULL),
    edgeAreaVec_(NULL),
    assembleNodalGradAlgDriver_(new AssembleNodalGradAlgorithmDriver(realm_, "temperature", "dtdx")),
    isInit_(true),
    projectedNodalGradEqs_(NULL)
{
    // extract solver name and solver object
    std::string solverName = realm_.equationSystems_.get_solver_block_name("temperature");
    LinearSolver * solver = realm_.root()->linearSolvers_->create_solver(solverName, EQ_TEMPERATURE);
    linsys_ = LinearSystem::create(realm_, 1, this, solver);

    // determine nodal gradient form
    set_nodal_gradient("temperature");
    HOFlowEnv::self().hoflowOutputP0() << "Edge projected nodal gradient for temperature: " << edgeNodalGradient_ <<std::endl;

    // push back EQ to manager
    realm_.push_equation_to_systems(this);

    // create projected nodal gradient equation system
    if ( managePNG_ ) {
        manage_png(eqSystems);
    }
}

HeatCondEquationSystem::~HeatCondEquationSystem() {
    delete assembleNodalGradAlgDriver_;
}

void HeatCondEquationSystem::load(const YAML::Node & node) {
    EquationSystem::load(node);
}

void HeatCondEquationSystem::manage_png(EquationSystems & eqSystems) {
    projectedNodalGradEqs_  = new ProjectedNodalGradientEquationSystem(eqSystems, EQ_PNG, "dtdx", "qTmp", "temperature", "PNGGradEQS");
    
    // fill the map; only require wall (which is the same name)...
    projectedNodalGradEqs_->set_data_map(WALL_BC, "temperature");
}

void HeatCondEquationSystem::register_nodal_fields(stk::mesh::Part *part) {
    stk::mesh::MetaData & meta_data = realm_.meta_data();

    const int nDim = meta_data.spatial_dimension();

    const int numStates = realm_.number_of_states();

    // register dof; set it as a restart variable
    temperature_ = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "temperature", numStates));
    stk::mesh::put_field_on_mesh(*temperature_, *part, nullptr);
    realm_.augment_restart_variable_list("temperature");

    dtdx_ =  &(meta_data.declare_field<VectorFieldType>(stk::topology::NODE_RANK, "dtdx"));
    stk::mesh::put_field_on_mesh(*dtdx_, *part, nDim, nullptr);

    // delta solution for linear solver
    tTmp_ =  &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "tTmp"));
    stk::mesh::put_field_on_mesh(*tTmp_, *part, nullptr);

    dualNodalVolume_ = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "dual_nodal_volume"));
    stk::mesh::put_field_on_mesh(*dualNodalVolume_, *part, nullptr);

    coordinates_ =  &(meta_data.declare_field<VectorFieldType>(stk::topology::NODE_RANK, "coordinates"));
    stk::mesh::put_field_on_mesh(*coordinates_, *part, nDim, nullptr);

    // props
    density_ = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "density"));
    stk::mesh::put_field_on_mesh(*density_, *part, nullptr);

    specHeat_ = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "specific_heat"));
    stk::mesh::put_field_on_mesh(*specHeat_, *part, nullptr);

    thermalCond_ = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "thermal_conductivity"));
    stk::mesh::put_field_on_mesh(*thermalCond_, *part, nullptr);

    // push to property list
    realm_.augment_property_map(DENSITY_ID, density_);
    realm_.augment_property_map(SPEC_HEAT_ID, specHeat_);
    realm_.augment_property_map(THERMAL_COND_ID, thermalCond_);

    // make sure all states are properly populated (restart can handle this)
    if ( numStates > 2 && (!realm_.restarted_simulation() || realm_.support_inconsistent_restart()) ) {
        ScalarFieldType &tempN = temperature_->field_of_state(stk::mesh::StateN);
        ScalarFieldType &tempNp1 = temperature_->field_of_state(stk::mesh::StateNP1);

        CopyFieldAlgorithm * theCopyAlgA = new CopyFieldAlgorithm(realm_, part, &tempNp1, &tempN, 0, 1, stk::topology::NODE_RANK);
        copyStateAlg_.push_back(theCopyAlgA);
    }
}

void HeatCondEquationSystem::register_edge_fields(stk::mesh::Part *part) {
    // can be deleted but safely
}

void HeatCondEquationSystem::register_element_fields(stk::mesh::Part * part, const stk::topology & theTopo) {
    // can be deleted but safely
}

void HeatCondEquationSystem::register_interior_algorithm(stk::mesh::Part *part) {
    // types of algorithms
    const AlgorithmType algType = INTERIOR;

    ScalarFieldType & tempNp1 = temperature_->field_of_state(stk::mesh::StateNP1);
    VectorFieldType & dtdxNone = dtdx_->field_of_state(stk::mesh::StateNone);

    // non-solver; contribution to projected nodal gradient; allow for element-based shifted
    if (!managePNG_) {
        std::map<AlgorithmType, Algorithm *>::iterator it = assembleNodalGradAlgDriver_->algMap_.find(algType);
        
        // If algorithm is not present, create a new one
        if ( it == assembleNodalGradAlgDriver_->algMap_.end() ) {
            Algorithm *theAlg = NULL;
            theAlg = new AssembleNodalGradElemAlgorithm(realm_, part, &tempNp1, &dtdxNone, edgeNodalGradient_);
            assembleNodalGradAlgDriver_->algMap_[algType] = theAlg;
        }
        else {
            it->second->partVec_.push_back(part);
        }
    }

    // solver; interior edge/element contribution (diffusion)
    if (!realm_.solutionOptions_->useConsolidatedSolverAlg_) {
        // Not consolidated solver algorithm
        
        std::map<AlgorithmType, SolverAlgorithm *>::iterator itsi = solverAlgDriver_->solverAlgMap_.find(algType);
        
        // If algorithm is not present, create a new one
        if ( itsi == solverAlgDriver_->solverAlgMap_.end() ) {
            SolverAlgorithm * theSolverAlg = NULL;
            theSolverAlg = new AssembleScalarElemDiffSolverAlgorithm(realm_, part, this, &tempNp1, &dtdxNone, thermalCond_);
            solverAlgDriver_->solverAlgMap_[algType] = theSolverAlg;

//            // look for fully integrated source terms
//            std::map<std::string, std::vector<std::string> >::iterator isrc  = realm_.solutionOptions_->elemSrcTermsMap_.find("temperature");
//            if ( isrc != realm_.solutionOptions_->elemSrcTermsMap_.end() ) {
//                std::vector<std::string> mapNameVec = isrc->second;
//                for (size_t k = 0; k < mapNameVec.size(); ++k ) {
//                  std::string sourceName = mapNameVec[k];
//                  SupplementalAlgorithm * suppAlg = NULL;
//                  if (sourceName == "steady_3d_thermal" ) {
//                        suppAlg = new SteadyThermal3dContactSrcElemSuppAlgDep(realm_);
//                  }
//                  else if (sourceName == "FEM" ) {
//                        throw std::runtime_error("HeatCondElemSrcTerms::Error FEM must use consolidated approach");
//                  }
//                  else {
//                        throw std::runtime_error("HeatCondElemSrcTerms::Error Source term is not supported: " + sourceName);
//                  }
//                  HOFlowEnv::self().hoflowOutputP0() << "HeatCondElemSrcTerms::added() " << sourceName << std::endl;
//                  theSolverAlg->supplementalAlg_.push_back(suppAlg);
//                }
//            }
        }
        else {
            itsi->second->partVec_.push_back(part);
        } 
    }
    else {
        // Consolidated solver algorithm
        //========================================================================================
        // WIP... supplemental algs plug into one homogeneous kernel, AssembleElemSolverAlgorithm
        // currently valid for P=1 3D hex tet pyr wedge and P=2 3D hex
        //========================================================================================

        // extract topo from part
        stk::topology partTopo = part->topology();
        HOFlowEnv::self().hoflowOutputP0() << "The name of this part is " << partTopo.name() << std::endl;

        auto & solverAlgMap = solverAlgDriver_->solverAlgorithmMap_;

        AssembleElemSolverAlgorithm * solverAlg =  nullptr;
        bool solverAlgWasBuilt =  false;
        std::tie(solverAlg, solverAlgWasBuilt) = build_or_add_part_to_solver_alg(*this, *part, solverAlgMap);

        ElemDataRequests& dataPreReqs = solverAlg->dataNeededByKernels_;
        auto& activeKernels = solverAlg->activeKernels_;

        if (solverAlgWasBuilt) {
//          build_topo_kernel_if_requested<SteadyThermal3dContactSrcElemKernel>(
//            partTopo, *this, activeKernels, "steady_3d_thermal",
//            realm_.bulk_data(), *realm_.solutionOptions_, dataPreReqs
//          );
//
//          build_topo_kernel_if_requested<ScalarDiffElemKernel>(
//            partTopo, *this, activeKernels, "CVFEM_DIFF",
//            realm_.bulk_data(), *realm_.solutionOptions_,
//            temperature_, thermalCond_, dataPreReqs
//          );
//
//          build_fem_kernel_if_requested<ScalarDiffFemKernel>(
//            partTopo, *this, activeKernels, "FEM_DIFF",
//            realm_.bulk_data(), *realm_.solutionOptions_, temperature_, thermalCond_, dataPreReqs
//          );
//
//          report_invalid_supp_alg_names();
//          report_built_supp_alg_names();
        }
    }

    // time term; nodally lumped
    const AlgorithmType algMass = MASS;
    std::map<AlgorithmType, SolverAlgorithm *>::iterator itsm = solverAlgDriver_->solverAlgMap_.find(algMass);
    
    // If algorithm is not present, create a new one
    if ( itsm == solverAlgDriver_->solverAlgMap_.end() ) {
        // create the solver alg
        AssembleNodeSolverAlgorithm * theAlg = new AssembleNodeSolverAlgorithm(realm_, part, this);
        solverAlgDriver_->solverAlgMap_[algMass] = theAlg;

        if (realm_.simType_ == "transient") {
            // now create the supplemental alg for mass term
            if ( realm_.number_of_states() == 2 ) {
                // 1st order time accuracy, bdf1 = backwards euler aka implicit euler
                HeatCondMassBackwardEulerNodeSuppAlg * theMass = new HeatCondMassBackwardEulerNodeSuppAlg(realm_);
                theAlg->supplementalAlg_.push_back(theMass);
            }
            else {
                // 2nd order time accuracy, backward differentiation formula bdf2
                HeatCondMassBDF2NodeSuppAlg * theMass = new HeatCondMassBDF2NodeSuppAlg(realm_);
                theAlg->supplementalAlg_.push_back(theMass);
            }
        }

//        // Add src term supp alg...; limited number supported
//        std::map<std::string, std::vector<std::string> >::iterator isrc = realm_.solutionOptions_->srcTermsMap_.find("temperature");
//        if ( isrc != realm_.solutionOptions_->srcTermsMap_.end() ) {
//            std::vector<std::string> mapNameVec = isrc->second;
//            for (size_t k = 0; k < mapNameVec.size(); ++k ) {
//                std::string sourceName = mapNameVec[k];
//                if (sourceName == "steady_2d_thermal" ) {
//                    SteadyThermalContactSrcNodeSuppAlg *theSrc
//                      = new SteadyThermalContactSrcNodeSuppAlg(realm_);
//                    HOFlowEnv::self().hoflowOutputP0() << "HeatCondNodalSrcTerms::added() " << sourceName << std::endl;
//
//                    theAlg->supplementalAlg_.push_back(theSrc);
//                }
//                else if (sourceName == "steady_3d_thermal" ) {
//                    SteadyThermalContact3DSrcNodeSuppAlg *theSrc
//                    = new SteadyThermalContact3DSrcNodeSuppAlg(realm_);
//                    theAlg->supplementalAlg_.push_back(theSrc);
//                }
//                else {
//                    throw std::runtime_error("HeatCondNodalSrcTerms::Error Source term is not supported: " + sourceName);
//                }
//            }
//        }
    }
    else {
        itsm->second->partVec_.push_back(part);
    }
}

void HeatCondEquationSystem::register_wall_bc(stk::mesh::Part * part,
                                              const stk::topology & partTopo,
                                              const WallBoundaryConditionData & wallBCData)
{
    const AlgorithmType algType = WALL;

    // np1
    ScalarFieldType & tempNp1 = temperature_->field_of_state(stk::mesh::StateNP1);
    VectorFieldType & dtdxNone = dtdx_->field_of_state(stk::mesh::StateNone);

    stk::mesh::MetaData & meta_data = realm_.meta_data();
    stk::mesh::BulkData & bulk_data = realm_.bulk_data();

    // non-solver; dtdx; allow for element-based shifted; all bcs are of generic type "WALL"
    if ( !managePNG_ ) {
        std::map<AlgorithmType, Algorithm *>::iterator it = assembleNodalGradAlgDriver_->algMap_.find(algType);
        
        // If algorithm is not present, create a new one
        if ( it == assembleNodalGradAlgDriver_->algMap_.end() ) {
            Algorithm * theAlg = new AssembleNodalGradBoundaryAlgorithm(realm_, part, & tempNp1, &dtdxNone, edgeNodalGradient_);
            assembleNodalGradAlgDriver_->algMap_[algType] = theAlg;
        }
        else {
            it->second->partVec_.push_back(part);
        }
    }

    // extract the value for user specified temperaure and save off the AuxFunction
    WallUserData userData = wallBCData.userData_;
    std::string temperatureName = "temperature";
    UserDataType theDataType = get_bc_data_type(userData, temperatureName);
    
    // If temperature specified (Dirichlet)
    if ( userData.tempSpec_ ||  FUNCTION_UD == theDataType ) {

        // register boundary data; temperature_bc
        ScalarFieldType * theBcField = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "temperature_bc"));
        stk::mesh::put_field_on_mesh(*theBcField, *part, nullptr);

        AuxFunction * theAuxFunc = NULL;
        if ( CONSTANT_UD == theDataType ) {
            Temperature theTemp = userData.temperature_;
            std::vector<double> userSpec(1);
            userSpec[0] = theTemp.temperature_;
            // new it
            theAuxFunc = new ConstantAuxFunction(0, 1, userSpec);
        }
        /*else {
            // extract the name
            std::string fcnName = get_bc_function_name(userData, temperatureName);
            // switch on the name found...
            if ( fcnName == "steady_3d_thermal" ) {
              theAuxFunc = new SteadyThermal3dContactAuxFunction();
            }
            else {
              throw std::runtime_error("Only steady_3d_thermal user functions supported");
            }
        }*/

        // bc data alg
        AuxFunctionAlgorithm * auxAlg = new AuxFunctionAlgorithm(realm_, part,
                                                                theBcField, theAuxFunc,
                                                                stk::topology::NODE_RANK);
        bcDataAlg_.push_back(auxAlg);

        // copy temperature_bc to temperature np1...
        CopyFieldAlgorithm * theCopyAlg = new CopyFieldAlgorithm(realm_, part,
                                                                theBcField, &tempNp1,
                                                                0, 1,
                                                                stk::topology::NODE_RANK);
        bcDataMapAlg_.push_back(theCopyAlg);

        // wall specified temperature solver algorithm
        // Dirichlet bc
        std::map<AlgorithmType, SolverAlgorithm *>::iterator itd = solverAlgDriver_->solverDirichAlgMap_.find(algType);

        // If algorithm is not present, create a new one
        if ( itd == solverAlgDriver_->solverDirichAlgMap_.end() ) {

            if (realm_.solutionOptions_->useNaluBC_) {
                // Use Nalu style Dirichlet BC, fix the nodal value to the specified value
                DirichletBC * theAlg = new DirichletBC(realm_, this, part, &tempNp1, theBcField, 0, 1);
                solverAlgDriver_->solverDirichAlgMap_[algType] = theAlg;
            }
            else {
                // Use CFX style Dirichlet BC, fix the IP value to the specified value
                AssembleScalarDirichletBCSolverAlgorithm * theAlg = new AssembleScalarDirichletBCSolverAlgorithm(realm_, part, this, theBcField);
                solverAlgDriver_->solverDirichAlgMap_[algType] = theAlg;
            }

        }
        else {
            itd->second->partVec_.push_back(part);
        }
    }
    // If heat flux is specified (Neumann)
    else if( userData.heatFluxSpec_ ) { // used to contain the following: && !userData.robinParameterSpec_

        const AlgorithmType algTypeHF = WALL_HF;
        
        ScalarFieldType * theBcNodalField = 0;
        ScalarFieldType * theBcField = 0;
        stk::mesh::EntityRank entity_rank;
        NormalHeatFlux heatFlux = userData.q_;
        
        if (realm_.solutionOptions_->useNaluBC_) {
            entity_rank = stk::topology::NODE_RANK;
            
            theBcNodalField = &(meta_data.declare_field<ScalarFieldType>(entity_rank, "heat_flux_bc"));
            stk::mesh::put_field_on_mesh(*theBcNodalField, *part, nullptr);
            
            std::vector<double> userSpec(1);
            userSpec[0] = heatFlux.qn_;

            // new it
            ConstantAuxFunction * theAuxFunc = new ConstantAuxFunction(0, 1, userSpec);

            // bc data alg
            AuxFunctionAlgorithm * auxAlg = new AuxFunctionAlgorithm(realm_, part,
                                                                    theBcNodalField, theAuxFunc,
                                                                    entity_rank);
            bcDataAlg_.push_back(auxAlg); 
        }
        else {
            entity_rank = static_cast<stk::topology::rank_t>(meta_data.side_rank());
            double bc_value = heatFlux.qn_;
            
            theBcField = &(meta_data.declare_field<ScalarFieldType>(entity_rank, "heat_flux_bc"));
            stk::mesh::put_field_on_mesh(*theBcField, *part, nullptr);
            
            // bc data alg
            ConstantBCAuxFunctionAlgorithm * auxAlg 
                    = new ConstantBCAuxFunctionAlgorithm(realm_, part, theBcField, entity_rank, bc_value);
            
            bcDataAlg_.push_back(auxAlg); 
        }

        // solver; lhs; same for edge and element-based scheme
        std::map<AlgorithmType, SolverAlgorithm *>::iterator itsi = solverAlgDriver_->solverAlgMap_.find(algTypeHF);
        if ( itsi == solverAlgDriver_->solverAlgMap_.end() ) {
            
            if (realm_.solutionOptions_->useNaluBC_) {
                // Use Nalu style Neumann BC, apply the boundary values to the nodes
                AssembleScalarFluxBCSolverAlgorithmNalu * theAlg = new AssembleScalarFluxBCSolverAlgorithmNalu(realm_, part, this, theBcNodalField);
                solverAlgDriver_->solverAlgMap_[algTypeHF] = theAlg;
            }
            else {
                // Use CFX style Neumann BC, apply the boundary values to the IPs
                AssembleScalarFluxBCSolverAlgorithm * theAlg = new AssembleScalarFluxBCSolverAlgorithm(realm_, part, this);
                solverAlgDriver_->solverAlgMap_[algTypeHF] = theAlg;
            }
        }
        else {
            itsi->second->partVec_.push_back(part);
        }
    }
    /*else if ( userData.irradSpec_ ) {

        const AlgorithmType algTypeRAD = WALL_RAD;

        // check for emissivity
        if ( !userData.emissSpec_)
          throw std::runtime_error("Sorry, irradiation was specified while emissivity was not");

        // register boundary data;
        ScalarFieldType *irradField = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "irradiation"));
        stk::mesh::put_field_on_mesh(*irradField, *part, nullptr);
        ScalarFieldType *emissField = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "emissivity"));
        stk::mesh::put_field_on_mesh(*emissField, *part, nullptr);

         aux algs; irradiation
        Irradiation irrad = userData.irradiation_;
        std::vector<double> irradUserSpec(1);
        irradUserSpec[0] = irrad.irradiation_;
        AuxFunction *irradAuxFunc = new ConstantAuxFunction(0, 1, irradUserSpec);

        AuxFunctionAlgorithm *irradAuxAlg
          = new AuxFunctionAlgorithm(realm_, part,
              irradField, irradAuxFunc,
              stk::topology::NODE_RANK);

        // aux algs; emissivity
        Emissivity emiss = userData.emissivity_;
        std::vector<double> emissUserSpec(1);
        emissUserSpec[0] = emiss.emissivity_;
        AuxFunction *emissAuxFunc = new ConstantAuxFunction(0, 1, emissUserSpec);

        AuxFunctionAlgorithm *emissAuxAlg
          = new AuxFunctionAlgorithm(realm_, part,
              emissField, emissAuxFunc,
              stk::topology::NODE_RANK);

        // if this is a multi-physics coupling, only populate IC for irradiation (xfer will handle it)
        if ( userData.isInterface_ ) {
          // xfer will handle population; only need to populate the initial value
          realm_.initCondAlg_.push_back(irradAuxAlg);
        }
        else {
          // put it on bcData
          bcDataAlg_.push_back(irradAuxAlg);
        }

        // emissivity is placed on bc data (never via XFER)
        bcDataAlg_.push_back(emissAuxAlg);

        // solver; lhs
        std::map<AlgorithmType, SolverAlgorithm *>::iterator itsi =
          solverAlgDriver_->solverAlgMap_.find(algTypeRAD);
        if ( itsi == solverAlgDriver_->solverAlgMap_.end() ) {
          AssembleHeatCondIrradWallSolverAlgorithm *theAlg
            = new AssembleHeatCondIrradWallSolverAlgorithm(realm_, part, this,
                realm_.realmUsesEdges_);
          solverAlgDriver_->solverAlgMap_[algTypeRAD] = theAlg;
        }
        else {
          itsi->second->partVec_.push_back(part);
        }
    }*/
    /*else if ( userData.htcSpec_ || userData.refTempSpec_ || userData.robinParameterSpec_ ) {

        const AlgorithmType algTypeCHT = WALL_CHT;

        // If the user specified a Robin parameter, this is a Robin-type CHT; otherwise, it's convection
        bool isRobinCHT = userData.robinParameterSpec_;
        bool isConvectionCHT = !isRobinCHT;

        // first make sure all appropriate variables were specified
        if (isConvectionCHT)
        {
          if ( !userData.refTempSpec_)
            throw std::runtime_error("Sorry, h was specified while Tref was not");
          if ( !userData.htcSpec_)
            throw std::runtime_error("Sorry, Tref was specified while h was not");
        }
        else
        {
          if ( !userData.refTempSpec_)
            throw std::runtime_error("Sorry, Robin parameter was specified while Tref was not");
          if ( !userData.heatFluxSpec_)
            throw std::runtime_error("Sorry, Robin parameter was specified while heat flux was not");
        }

        // register boundary data
        ScalarFieldType *normalHeatFluxField = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "normal_heat_flux"));
        stk::mesh::put_field_on_mesh(*normalHeatFluxField, *part, nullptr);
        ScalarFieldType *tRefField = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "reference_temperature"));
        stk::mesh::put_field_on_mesh(*tRefField, *part, nullptr);

        ScalarFieldType *alphaField = NULL;
        if (isConvectionCHT)
        {
          alphaField = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "heat_transfer_coefficient"));
        }
        if (isRobinCHT)
        {
          alphaField = &(meta_data.declare_field<ScalarFieldType>(stk::topology::NODE_RANK, "robin_coupling_parameter"));
        }
        stk::mesh::put_field_on_mesh(*alphaField, *part, nullptr);

        // aux algs
        AuxFunctionAlgorithm * alphaAuxAlg;
        if (isRobinCHT)
        {
          RobinCouplingParameter alpha = userData.robinCouplingParameter_;
          std::vector<double> alphaUserSpec(1);
          alphaUserSpec[0] = alpha.robinCouplingParameter_;
          AuxFunction *alphaAuxFunc = new ConstantAuxFunction(0, 1, alphaUserSpec);
          alphaAuxAlg = new AuxFunctionAlgorithm(realm_,
                                                 part,
                                                 alphaField,
                                                 alphaAuxFunc,
                                                 stk::topology::NODE_RANK);
        }

        AuxFunctionAlgorithm * htcAuxAlg;
        if (isConvectionCHT)
        {
          HeatTransferCoefficient htc = userData.heatTransferCoefficient_;
          std::vector<double> htcUserSpec(1);
          htcUserSpec[0] = htc.heatTransferCoefficient_;
          AuxFunction *htcAuxFunc = new ConstantAuxFunction(0, 1, htcUserSpec);
          htcAuxAlg = new AuxFunctionAlgorithm(realm_, 
                                               part,
                                               alphaField, 
                                               htcAuxFunc,
                                               stk::topology::NODE_RANK);
        }

        NormalHeatFlux heatFlux = userData.q_;
        std::vector<double> qnUserSpec(1);
        // For convection, pass a zero heat flux field; for Robin, use specified value
        qnUserSpec[0] = (isRobinCHT ? heatFlux.qn_ : 0.0);
        AuxFunction *qnAuxFunc = new ConstantAuxFunction(0, 1, qnUserSpec);
        AuxFunctionAlgorithm *qnAuxAlg = new AuxFunctionAlgorithm(realm_,
                                                                  part,
                                                                  normalHeatFluxField,
                                                                  qnAuxFunc,
                                                                  stk::topology::NODE_RANK);

        ReferenceTemperature tRef = userData.referenceTemperature_;
        std::vector<double> tRefUserSpec(1);
        tRefUserSpec[0] = tRef.referenceTemperature_;
        AuxFunction *tRefAuxFunc = new ConstantAuxFunction(0, 1, tRefUserSpec);
        AuxFunctionAlgorithm *tRefAuxAlg = new AuxFunctionAlgorithm(realm_, 
                                                                    part,
                                                                    tRefField, 
                                                                    tRefAuxFunc,
                                                                    stk::topology::NODE_RANK);


        // decide where to put population of data
        // Normal heat flux, reference temperature, and coupling parameter
        // come from a transfer if this is an interface, so in that case
        // only need to populate the initial values
        if (userData.isInterface_) {
          // xfer will handle population; only need to populate the initial value
          realm_.initCondAlg_.push_back(tRefAuxAlg);
          if (isRobinCHT) 
          {
            realm_.initCondAlg_.push_back(alphaAuxAlg);
            realm_.initCondAlg_.push_back(qnAuxAlg);
          }
          if (isConvectionCHT) realm_.initCondAlg_.push_back(htcAuxAlg);
        }
        else {
          // put it on bcData
          bcDataAlg_.push_back(tRefAuxAlg);
          if (isRobinCHT)
          {
            bcDataAlg_.push_back(alphaAuxAlg);
            bcDataAlg_.push_back(qnAuxAlg);
          }
          if (isConvectionCHT) bcDataAlg_.push_back(htcAuxAlg);
        }
        // For convection-type, normal heat flux remains zero -- just set at IC
        if (isConvectionCHT) realm_.initCondAlg_.push_back(qnAuxAlg);

        // solver contribution
        std::map<AlgorithmType, SolverAlgorithm *>::iterator itsi =
          solverAlgDriver_->solverAlgMap_.find(algTypeCHT);
        if ( itsi == solverAlgDriver_->solverAlgMap_.end() ) {
          AssembleHeatCondWallSolverAlgorithm *theAlg
            = new AssembleHeatCondWallSolverAlgorithm(realm_, 
                                                      part, 
                                                      this,
                                                      tRefField,
                                                      alphaField,
                                                      normalHeatFluxField,
                                                      realm_.realmUsesEdges_);
          solverAlgDriver_->solverAlgMap_[algTypeCHT] = theAlg;
        }
        else {
          itsi->second->partVec_.push_back(part);
        }

    }*/
}

void HeatCondEquationSystem::initialize() {
    solverAlgDriver_->initialize_connectivity();
    linsys_->finalizeLinearSystem();
}

void HeatCondEquationSystem::reinitialize_linear_system() {
    // delete linsys
    delete linsys_;

    // delete old solver
    const EquationType theEqID = EQ_TEMPERATURE;
    LinearSolver *theSolver = NULL;
    std::map<EquationType, LinearSolver *>::const_iterator iter
      = realm_.root()->linearSolvers_->solvers_.find(theEqID);
    if (iter != realm_.root()->linearSolvers_->solvers_.end()) {
        theSolver = (*iter).second;
        delete theSolver;
    }

    // create new solver
    std::string solverName = realm_.equationSystems_.get_solver_block_name("temperature");
    LinearSolver * solver = realm_.root()->linearSolvers_->create_solver(solverName, EQ_TEMPERATURE);
    linsys_ = LinearSystem::create(realm_, 1, this, solver);

    // initialize
    solverAlgDriver_->initialize_connectivity();
    linsys_->finalizeLinearSystem();
}

void HeatCondEquationSystem::predict_state() {
    // copy state n to state np1
    ScalarFieldType & tN = temperature_->field_of_state(stk::mesh::StateN);
    ScalarFieldType & tNp1 = temperature_->field_of_state(stk::mesh::StateNP1);
    field_copy(realm_.meta_data(), realm_.bulk_data(), tN, tNp1, realm_.get_activate_aura());
}

void HeatCondEquationSystem::solve_and_update() {
    // initialize fields
    if ( isInit_ ) {
        compute_projected_nodal_gradient();
        isInit_ = false;
    }
    
    // Linear Solving Loop
    for ( int k = 0; k < maxIterations_; ++k ) {
        HOFlowEnv::self().hoflowOutputP0() << " " << k+1 << "/" << maxIterations_
                                           << std::setw(15) << std::right << userSuppliedName_ << std::endl;

        // heat conduction assemble, load_complete and solve for tTmp (delta solution)
        assemble_and_solve(tTmp_);

        // update
        double timeA = HOFlowEnv::self().hoflow_time();
        
        // Add tTmp (delta solution) to current solution temperature
        field_axpby(
            realm_.meta_data(),
            realm_.bulk_data(),
            1.0, *tTmp_,
            1.0, *temperature_,
            realm_.get_activate_aura());
        double timeB = HOFlowEnv::self().hoflow_time();
        timerAssemble_ += (timeB-timeA);

        // projected nodal gradient
        timeA = HOFlowEnv::self().hoflow_time();
        compute_projected_nodal_gradient();
        timeB = HOFlowEnv::self().hoflow_time();
        timerMisc_ += (timeB-timeA);
    }  
}

void HeatCondEquationSystem::compute_projected_nodal_gradient() {
    if ( !managePNG_ ) {
        assembleNodalGradAlgDriver_->execute();
    }
    else {
        projectedNodalGradEqs_->solve_and_update_external();
    }
}