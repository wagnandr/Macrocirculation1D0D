////////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2019 Prashant K. Jha
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
////////////////////////////////////////////////////////////////////////////////

#ifndef UTIL_SEG_FV_H
#define UTIL_SEG_FV_H

#include "utils.hpp"
#include "list_structure.hpp"

namespace util {

struct ElemWeights {

  unsigned id_seg;
  double half_cyl_surf;
  std::vector<unsigned int> elem_id;
  std::vector<double> elem_weight;

  ElemWeights(unsigned int id = 0) : id_seg(id), half_cyl_surf(0.) {};

  void add_unique(const unsigned int &elem, const double &weight) {

    for (unsigned int i=0; i<elem_id.size(); i++) {
      if (elem_id[i] == elem) {
        elem_weight[i] += weight;
        return;
      }
    }

    elem_id.push_back(elem);
    elem_weight.push_back(weight);
  }
};

enum TypeOfSegment { DirBoundary, Inner };

class SegFV {

public:
  /*!
   * @brief Constructor
   */

  SegFV(): index(0), typeOfSegment(Inner), length(0.0),
           radius(0.0), L_p(0.0), p_boundary_1(0.0), p_boundary_2(0.0),
           p_v(0.0), global_successor(NULL),
           index_1(0), index_2(0), coord_1(0.0), coord_2(0.0)
  {}

  std::vector<std::shared_ptr<SegFV>> neighbors_1;

  std::vector<std::shared_ptr<SegFV>> neighbors_2;

  int index, index_1, index_2;

  std::vector<double> coord_1, coord_2;

  TypeOfSegment typeOfSegment;

  double length, radius, L_p, p_boundary_1, p_boundary_2, t_seg, p_v, mu;

  std::shared_ptr<SegFV> global_successor;

  double getTransmissibilty() {

    double t = 0.0;

    t = (2.0 * M_PI * radius * radius * radius * radius) / (8.0 * length * mu);

    return t;

  }

};

enum TypeOfNode{DirichletNode,InnerNode};

class VGNode{

public:
  /*!
   * @brief Constructor
   */
  VGNode(): index(0), p_v(0.0), c_v(0.0), p_boundary(0.0), 
            c_boundary(0.0), edge_touched(false), sprouting_edge(false), apicalGrowth(false),
            coord(0.0), radii(0.0), L_p(0.0)           
  {}

  int index;

  bool apicalGrowth;

  double p_v, c_v, p_boundary, c_boundary;

  std::vector< double > coord, radii, L_p, L_s;

  std::vector< bool > edge_touched;

  std::vector< bool > sprouting_edge;

  std::vector< std::shared_ptr<VGNode> > neighbors;

  TypeOfNode typeOfVGNode;

  std::shared_ptr<VGNode> global_successor;

  std::vector<ElemWeights> J_b_points;

  void markEdge( int index ){

       int numberOfNeighbors = neighbors.size();

       for(int i=0;i<numberOfNeighbors;i++){

           if( index == neighbors[i]->index ){

               edge_touched[ i ] = true;

           }

       }

  }

  void markEdgeLocalIndex( int localIndex ){

       edge_touched[ localIndex ] = true;

  }

  int getLocalIndexOfNeighbor( std::shared_ptr<VGNode> neighbor ){

      int local_index_neighbor = 0;

      int numberOfNeighbors = neighbors.size();

      for(int i=0;i<numberOfNeighbors;i++){
      
          if( neighbor->index == neighbors[i]->index ){

              local_index_neighbor = i;

              return local_index_neighbor;

          }

      }

      return local_index_neighbor;

  }

  void replacePointerWithIndex( int index_new, std::shared_ptr<VGNode> new_pointer ){

       int numberOfNeighbors = neighbors.size();

       for(int i=0;i<numberOfNeighbors;i++){

           if( index_new == neighbors[i]->index ){

               neighbors[ i ] = new_pointer;

               edge_touched[ i ] = true;

           }

       }

  }


  void attachNeighbor( std::shared_ptr<VGNode> new_pointer ){

       neighbors.push_back( new_pointer );

       typeOfVGNode = InnerNode;       

  }

  double getTotalVolume(){

         double totalVolume = 0.0;

         int numberOfNeighbors = neighbors.size();

         for(int i=0;i<numberOfNeighbors;i++){

             std::vector<double> coord_neighbor = neighbors[ i ]->coord;

             double length = 0.0;

             for(int j=0;j<3;j++){

                 length += (coord[j]-coord_neighbor[j])*(coord[j]-coord_neighbor[j]);             
                  
             }

             length = std::sqrt(length);

             totalVolume = totalVolume + (M_PI*radii[ i ]*radii[ i ]*length/2.0);

         }

         return totalVolume;
  }

  void markEdgeForSprouting(int edgeNumber){

       sprouting_edge[ edgeNumber ] = true; 

  }

  void markNodeForApicalGrowth(){

       apicalGrowth = true;

  }

};

} // namespace util

#endif // UTIL_NETWORK_H