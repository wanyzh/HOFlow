/*------------------------------------------------------------------------*/
/*  HOFlow - Higher Order Flow                                            */
/*  CFD Solver based ond CVFEM                                            */
/*------------------------------------------------------------------------*/
#ifndef ASSEMBLEFACEELEMSOLVERALGORITHM_H
#define ASSEMBLEFACEELEMSOLVERALGORITHM_H

#include <SolverAlgorithm.h>
#include <ElemDataRequests.h>
#include <ScratchViews.h>
#include <SimdInterface.h>
#include <SharedMemData.h>
#include <CopyAndInterleave.h>

namespace stk {
namespace mesh {
class Part;
}
}

class Realm;

class AssembleFaceElemSolverAlgorithm : public SolverAlgorithm
{
public:
  AssembleFaceElemSolverAlgorithm(
    Realm &realm,
    stk::mesh::Part *part,
    EquationSystem *eqSystem,
    unsigned nodesPerFace,
    unsigned nodesPerElem,
    bool interleaveMeViews = true);
  virtual ~AssembleFaceElemSolverAlgorithm() {}
  virtual void initialize_connectivity();
  virtual void execute();

  template<typename LambdaFunction>
  void run_face_elem_algorithm(stk::mesh::BulkData& bulk, LambdaFunction lamdbaFunc)
  {
      int nDim = bulk.mesh_meta_data().spatial_dimension();

      MasterElement* meFC = faceDataNeeded_.get_cvfem_face_me();
      MasterElement* meSCS = faceDataNeeded_.get_cvfem_surface_me();
      MasterElement* meSCV = faceDataNeeded_.get_cvfem_volume_me();

      ScratchMeInfo meElemInfo;
      meElemInfo.nodalGatherSize_ = nodesPerElem_;
      meElemInfo.nodesPerFace_ = nodesPerFace_;
      meElemInfo.nodesPerElement_ = nodesPerElem_;
      meElemInfo.numFaceIp_ = meFC != nullptr ? meFC->numIntPoints_ : 0;
      meElemInfo.numScsIp_ = meSCS != nullptr ? meSCS->numIntPoints_ : 0;
      meElemInfo.numScvIp_ = meSCV != nullptr ? meSCV->numIntPoints_ : 0;

      int rhsSize = meElemInfo.nodalGatherSize_*numDof_, lhsSize = rhsSize*rhsSize, scratchIdsSize = rhsSize;

      const int bytes_per_team = 0;
      const int bytes_per_thread = calculate_shared_mem_bytes_per_thread(lhsSize, rhsSize, scratchIdsSize,
                                                                       nDim, faceDataNeeded_, elemDataNeeded_, meElemInfo);

      const bool interleaveMeViews = false;

      stk::mesh::Selector s_locally_owned_union = bulk.mesh_meta_data().locally_owned_part() 
        &stk::mesh::selectUnion(partVec_);
      stk::mesh::EntityRank sideRank = bulk.mesh_meta_data().side_rank();
      stk::mesh::BucketVector const& buckets = bulk.get_buckets(sideRank, s_locally_owned_union );

      auto team_exec = get_team_policy(buckets.size(), bytes_per_team, bytes_per_thread);
      Kokkos::parallel_for(team_exec, [&](const TeamHandleType& team)
      {
        stk::mesh::Bucket & b = *buckets[team.league_rank()];

        ThrowAssertMsg(b.topology().num_nodes() == (unsigned)nodesPerFace_,
                       "AssembleFaceElemSolverAlgorithm expected nodesPerEntity_ = "
                       <<nodesPerFace_<<", but b.topology().num_nodes() = "<<b.topology().num_nodes());

        SharedMemData_FaceElem smdata(team, bulk, faceDataNeeded_, elemDataNeeded_, meElemInfo, rhsSize);

        const size_t bucketLen   = b.size();
        const size_t simdBucketLen = get_num_simd_groups(bucketLen);

        Kokkos::parallel_for(Kokkos::TeamThreadRange(team, simdBucketLen), [&](const size_t& bktIndex)
        {
          size_t simdGroupLen = get_length_of_next_simd_group(bktIndex, bucketLen);
          size_t numFacesProcessed = 0;
          do {
            int elemFaceOrdinal = -1;
            int simdFaceIndex = 0;
            while((numFacesProcessed+simdFaceIndex)<simdGroupLen) {
              stk::mesh::Entity face = b[bktIndex*simdLen + numFacesProcessed + simdFaceIndex];
              ThrowAssertMsg(bulk.num_elements(face)==1, "Expecting just 1 element attached to face!");
              int thisElemFaceOrdinal = bulk.begin_element_ordinals(face)[0];

              if (elemFaceOrdinal >= 0 && thisElemFaceOrdinal != elemFaceOrdinal) {
                break;
              }

              const stk::mesh::Entity* elems = bulk.begin_elements(face);
  
              smdata.connectedNodes[simdFaceIndex] = bulk.begin_nodes(elems[0]);
              smdata.elemFaceOrdinal = thisElemFaceOrdinal;
              elemFaceOrdinal = thisElemFaceOrdinal;
              fill_pre_req_data(faceDataNeeded_, bulk, face, *smdata.faceViews[simdFaceIndex], interleaveMeViews);
              fill_pre_req_data(elemDataNeeded_, bulk, elems[0], *smdata.elemViews[simdFaceIndex], interleaveMeViews);
              ++simdFaceIndex;
            }
            smdata.numSimdFaces = simdFaceIndex;
            numFacesProcessed += simdFaceIndex;
  
            copy_and_interleave(smdata.faceViews, smdata.numSimdFaces, smdata.simdFaceViews, interleaveMeViews);
            copy_and_interleave(smdata.elemViews, smdata.numSimdFaces, smdata.simdElemViews, interleaveMeViews);
            fill_master_element_views(faceDataNeeded_, bulk, smdata.simdFaceViews, smdata.elemFaceOrdinal);
            fill_master_element_views(elemDataNeeded_, bulk, smdata.simdElemViews, smdata.elemFaceOrdinal);
  
            lamdbaFunc(smdata);
          } while(numFacesProcessed < simdGroupLen);
        });
      });
  }

  ElemDataRequests faceDataNeeded_;
  ElemDataRequests elemDataNeeded_;
  unsigned numDof_;
  unsigned nodesPerFace_;
  unsigned nodesPerElem_;
  int rhsSize_;
  const bool interleaveMEViews_;
};

#endif /* ASSEMBLEFACEELEMSOLVERALGORITHM_H */

