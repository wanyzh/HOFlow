/*------------------------------------------------------------------------*/
/*  HOFlow - Higher Order Flow                                            */
/*  CFD Solver based ond CVFEM                                            */
/*------------------------------------------------------------------------*/
#ifndef FIELDFUNCTIONS_H
#define FIELDFUNCTIONS_H

#include <stk_mesh/base/MetaData.hpp>

class stk::mesh::FieldBase;
class stk::mesh::BulkData;

/* 
   A note on design: 

   FieldFunction methods can intersect the desired field (set of fields)
   with the universal part as follows: 

   const stk::mesh::Selector selector =
      metaData.universal_part() &
      stk::mesh::selectField(xField) & stk::mesh::selectField(yField);

   The above selector would include aura-entities

   An alternative choice would be to intersect the desired field )set of fields)
   with the locally_owned or globally_shared part as follows:

   stk::mesh::Selector s_all_nodes
    = (metaData.locally_owned_part() | metaData.globally_shared_part())
    &stk::mesh::selectField(*myField);

   The above selector would exclude aura-entities
*/

// y = alpha*x + beta*y
void field_axpby(
  const stk::mesh::MetaData & metaData,
  const stk::mesh::BulkData & bulkData,
  const double alpha,
  const stk::mesh::FieldBase & xField,
  const double beta,
  const stk::mesh::FieldBase & yField,
  const bool auraIsActive,
  const stk::topology::rank_t entityRankValue=stk::topology::NODE_RANK);

// x = alpha
void field_fill(
  const stk::mesh::MetaData & metaData,
  const stk::mesh::BulkData & bulkData,
  const double alpha,
  const stk::mesh::FieldBase & xField,
  const bool auraIsActive,
  const stk::topology::rank_t entityRankValue=stk::topology::NODE_RANK);

// x = alpha * x
void field_scale(
  const stk::mesh::MetaData & metaData,
  const stk::mesh::BulkData & bulkData,
  const double alpha,
  const stk::mesh::FieldBase & xField,
  const bool auraIsActive,
  const stk::topology::rank_t entityRankValue=stk::topology::NODE_RANK);

// y = x
void field_copy(
  const stk::mesh::MetaData & metaData,
  const stk::mesh::BulkData & bulkData,
  const stk::mesh::FieldBase & xField,
  const stk::mesh::FieldBase & yField,
  const bool auraIsActive,
  const stk::topology::rank_t entityRankValue=stk::topology::NODE_RANK);

// y[compJ] = x[compK]
void field_index_copy(
  const stk::mesh::MetaData & metaData,
  const stk::mesh::BulkData & bulkData,
  const stk::mesh::FieldBase & xField,
  const int xFieldIndex,
  const stk::mesh::FieldBase & yField,
  const int yFieldIndex,
  const bool auraIsActive,
  const stk::topology::rank_t entityRankValue=stk::topology::NODE_RANK);

#endif /* FIELDFUNCTIONS_H */

