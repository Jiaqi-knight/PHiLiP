#include<fstream>
#include <deal.II/base/parameter_handler.h>
#include <deal.II/base/tensor.h>

#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/grid_refinement.h>

#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_tools.h>
#include <deal.II/dofs/dof_renumbering.h>

#include <deal.II/lac/vector.h>
#include <deal.II/lac/dynamic_sparsity_pattern.h>
#include <deal.II/lac/sparse_matrix.h>

//#include <deal.II/lac/solver_control.h>
//#include <deal.II/lac/trilinos_precondition.h>
//#include <deal.II/lac/trilinos_solver.h>

#include <deal.II/meshworker/dof_info.h>
#include <deal.II/meshworker/integration_info.h>
#include <deal.II/meshworker/simple.h>
#include <deal.II/meshworker/loop.h>


// Finally, we take our exact solution from the library as well as volume_quadrature
// and additional tools.
#include <deal.II/numerics/data_out.h>
#include <deal.II/numerics/data_out_dof_data.h>


#include "dg.h"
#include "post_processor/euler_post.h"

namespace PHiLiP {

// DGFactory ***********************************************************************
template <int dim, typename real>
std::shared_ptr< DGBase<dim,real> >
DGFactory<dim,real>
::create_discontinuous_galerkin(
    const Parameters::AllParameters *const parameters_input,
    const unsigned int degree)
{
    using PDE_enum = Parameters::AllParameters::PartialDifferentialEquation;

    PDE_enum pde_type = parameters_input->pde_type;
    //if (pde_type == PDE_enum::advection) {
    //    return new DG<dim,1,real>(parameters_input, degree);
    //} else if (pde_type == PDE_enum::diffusion) {
    //    return new DG<dim,1,real>(parameters_input, degree);
    //} else if (pde_type == PDE_enum::convection_diffusion) {
    //    return new DG<dim,1,real>(parameters_input, degree);
    //}

    if (parameters_input->use_weak_form) {
        if (pde_type == PDE_enum::advection) {
            return std::make_shared< DGWeak<dim,1,real> >(parameters_input, degree);
        } else if (pde_type == PDE_enum::advection_vector) {
            return std::make_shared< DGWeak<dim,2,real> >(parameters_input, degree);
        } else if (pde_type == PDE_enum::diffusion) {
            return std::make_shared< DGWeak<dim,1,real> >(parameters_input, degree);
        } else if (pde_type == PDE_enum::convection_diffusion) {
            return std::make_shared< DGWeak<dim,1,real> >(parameters_input, degree);
        } else if (pde_type == PDE_enum::burgers_inviscid) {
            return std::make_shared< DGWeak<dim,dim,real> >(parameters_input, degree);
        } else if (pde_type == PDE_enum::euler) {
            return std::make_shared< DGWeak<dim,dim+2,real> >(parameters_input, degree);
        }
    } else {
        if (pde_type == PDE_enum::advection) {
            return std::make_shared< DGStrong<dim,1,real> >(parameters_input, degree);
        } else if (pde_type == PDE_enum::advection_vector) {
            return std::make_shared< DGStrong<dim,2,real> >(parameters_input, degree);
        } else if (pde_type == PDE_enum::diffusion) {
            return std::make_shared< DGStrong<dim,1,real> >(parameters_input, degree);
        } else if (pde_type == PDE_enum::convection_diffusion) {
            return std::make_shared< DGStrong<dim,1,real> >(parameters_input, degree);
        } else if (pde_type == PDE_enum::burgers_inviscid) {
            return std::make_shared< DGStrong<dim,dim,real> >(parameters_input, degree);
        } else if (pde_type == PDE_enum::euler) {
            return std::make_shared< DGStrong<dim,dim+2,real> >(parameters_input, degree);
        }
    }
    std::cout << "Can't create DGBase in create_discontinuous_galerkin(). Invalid PDE type: " << pde_type << std::endl;
    return nullptr;
}

// DGBase ***************************************************************************
template <int dim, typename real>
DGBase<dim,real>::DGBase( // @suppress("Class members should be properly initialized")
    const int nstate_input,
    const Parameters::AllParameters *const parameters_input,
    const unsigned int degree)
    :
    nstate(nstate_input)
    , mapping(degree+3,true)
    , fe_dg(degree)
    , fe_system(fe_dg, nstate)
    , all_parameters(parameters_input)
    , oned_quadrature (degree+1)
    , volume_quadrature (degree+1)
    , face_quadrature (degree+1)
{
	if (parameters_input->use_collocated_nodes)
	{
		dealii::QGaussLobatto<1> oned_quad_Gauss_Lobatto (degree+1);
		dealii::QGaussLobatto<dim> vol_quad_Gauss_Lobatto (degree+1);
		oned_quadrature = oned_quad_Gauss_Lobatto;
		volume_quadrature = vol_quad_Gauss_Lobatto;

		if(dim == 1)
		{
			dealii::QGauss<dim-1> face_quad_Gauss_Legendre (degree+1);
			face_quadrature = face_quad_Gauss_Legendre;
		}
		else
		{
			dealii::QGaussLobatto<dim-1> face_quad_Gauss_Lobatto (degree+1);
			face_quadrature = face_quad_Gauss_Lobatto;
		}


	}
	else
	{
		dealii::QGauss<1> oned_quad_Gauss_Legendre (degree+1);
		dealii::QGauss<dim> vol_quad_Gauss_Legendre (degree+1);
		dealii::QGauss<dim-1> face_quad_Gauss_Legendre (degree+1);
		oned_quadrature = oned_quad_Gauss_Legendre;
		volume_quadrature = vol_quad_Gauss_Legendre;
		face_quadrature = face_quad_Gauss_Legendre;
	}


}

// Destructor
template <int dim, typename real>
DGBase<dim,real>::~DGBase () { }

template <int dim, typename real>
void DGBase<dim,real>::allocate_system ()
{
    std::cout << std::endl << "Allocating DGWeak system and initializing FEValues" << std::endl;
    // This function allocates all the necessary memory to the 
    // system matrices and vectors.

    DGBase<dim,real>::dof_handler.initialize(*DGBase<dim,real>::triangulation, DGBase<dim,real>::fe_system);
    //DGBase<dim,real>::dof_handler.initialize(*DGBase<dim,real>::triangulation, DGBase<dim,real>::fe_system);
    // Allocates memory from triangulation and finite element space
    // Use fe_system since it will have the (fe_system.n_dofs)*nstate
    DGBase<dim,real>::dof_handler.distribute_dofs(DGBase<dim,real>::fe_system);

    //std::vector<unsigned int> block_component(nstate,0);
    //dealii::DoFRenumbering::component_wise(DGBase<dim,real>::dof_handler, block_component);

    // Allocate matrix
    unsigned int n_dofs = DGBase<dim,real>::dof_handler.n_dofs();
    //DynamicSparsityPattern dsp(n_dofs, n_dofs);
    DGBase<dim,real>::sparsity_pattern.reinit(n_dofs, n_dofs);

    dealii::DoFTools::make_flux_sparsity_pattern(DGBase<dim,real>::dof_handler, DGBase<dim,real>::sparsity_pattern);

    DGBase<dim,real>::system_matrix.reinit(DGBase<dim,real>::sparsity_pattern);

    // Allocate vectors
    DGBase<dim,real>::solution.reinit(n_dofs);
    DGBase<dim,real>::right_hand_side.reinit(n_dofs);

}

template <int dim, typename real>
void DGBase<dim,real>::assemble_residual ()
{
    DGBase<dim,real>::system_matrix = 0;
    DGBase<dim,real>::right_hand_side = 0;

    // For now assume same polynomial degree across domain
    const unsigned int dofs_per_cell = DGBase<dim,real>::dof_handler.get_fe().dofs_per_cell;
    std::vector<dealii::types::global_dof_index> current_dofs_indices (dofs_per_cell);
    std::vector<dealii::types::global_dof_index> neighbor_dofs_indices (dofs_per_cell);

    // ACTIVE cells, therefore, no children
    typename dealii::DoFHandler<dim>::active_cell_iterator
        current_cell = DGBase<dim,real>::dof_handler.begin_active(),
        endc = DGBase<dim,real>::dof_handler.end();

    unsigned int n_cell_visited = 0;
    unsigned int n_face_visited = 0;

    dealii::FEValues<dim,dim>        fe_values_cell (DGBase<dim,real>::mapping, DGBase<dim,real>::fe_system, DGBase<dim,real>::volume_quadrature, this->update_flags);
    dealii::FEFaceValues<dim,dim>    fe_values_face_int (DGBase<dim,real>::mapping, DGBase<dim,real>::fe_system, DGBase<dim,real>::face_quadrature, this->face_update_flags);
    dealii::FESubfaceValues<dim,dim> fe_values_subface_int (DGBase<dim,real>::mapping, DGBase<dim,real>::fe_system, DGBase<dim,real>::face_quadrature, this->face_update_flags);
    dealii::FEFaceValues<dim,dim>    fe_values_face_ext (DGBase<dim,real>::mapping, DGBase<dim,real>::fe_system, DGBase<dim,real>::face_quadrature, this->neighbor_face_update_flags);

    for (; current_cell!=endc; ++current_cell) {
        // std::cout << "Current cell index: " << current_cell->index() << std::endl;
        n_cell_visited++;

        // Local vector contribution from each cell
        dealii::Vector<double> current_cell_rhs (dofs_per_cell); // Defaults to 0.0 initialization

        fe_values_cell.reinit (current_cell);
        current_cell->get_dof_indices (current_dofs_indices);

        assemble_cell_terms_explicit (fe_values_cell, current_dofs_indices, current_cell_rhs);

        for (unsigned int iface=0; iface < dealii::GeometryInfo<dim>::faces_per_cell; ++iface) {

            typename dealii::DoFHandler<dim>::face_iterator current_face = current_cell->face(iface);
            typename dealii::DoFHandler<dim>::cell_iterator neighbor_cell = current_cell->neighbor(iface);

            // See tutorial step-30 for breakdown of 4 face cases

            // Case 1:
            // Face at boundary
            if (current_face->at_boundary() && !current_cell->has_periodic_neighbor(iface) ) {

                n_face_visited++;

                fe_values_face_int.reinit (current_cell, iface);

                //no need to worry about this for now.
                if(current_face->at_boundary() && all_parameters->use_periodic_bc == true && dim == 1) //using periodic BCs (for 1d)
                {
                  	int cell_index = current_cell->index();
                    if (cell_index == 0 && iface == 0)
                    {
                    	fe_values_face_int.reinit(current_cell, iface);
                        typename dealii::DoFHandler<dim>::active_cell_iterator neighbour_cell = dof_handler.begin_active();
                        for (unsigned int i = 0 ; i < triangulation->n_active_cells() - 1; ++i)
                        {
                        	++neighbour_cell;
                        }
                        neighbour_cell->get_dof_indices(neighbor_dofs_indices);
                        fe_values_face_ext.reinit(neighbour_cell,(iface == 1) ? 0 : 1);

                    }
                  	else if (cell_index == (int) triangulation->n_active_cells() - 1 && iface == 1)
                   	{
                  		fe_values_face_int.reinit(current_cell, iface);
                  		typename dealii::DoFHandler<dim>::active_cell_iterator neighbour_cell = dof_handler.begin_active();
                   		neighbour_cell->get_dof_indices(neighbor_dofs_indices);
                 		fe_values_face_ext.reinit(neighbour_cell,(iface == 1) ? 0 : 1); //not sure how changing the face number would work in dim!=1-dimensions.
                  	}
                }
                else
                {
					const unsigned int degree_current = DGBase<dim,real>::fe_system.tensor_degree();
					const unsigned int deg1sq = (degree_current == 0) ? 1 : degree_current * (degree_current+1);
					const unsigned int normal_direction = dealii::GeometryInfo<dim>::unit_normal_direction[iface];
					const real vol_div_facearea1 = current_cell->extent_in_direction(normal_direction);

					real penalty = deg1sq / vol_div_facearea1;
					//penalty = 1;//99;

					const unsigned int boundary_id = current_face->boundary_id();
					// Need to somehow get boundary type from the mesh
					assemble_boundary_term_explicit (boundary_id, fe_values_face_int, penalty, current_dofs_indices, current_cell_rhs);
                }

                //CASE 1.5: periodic boundary conditions
                //note that periodicity is not adapted for hp adaptivity yet. this needs to be figured out in the future
            } else if (current_face->at_boundary() && current_cell->has_periodic_neighbor(iface)){

            	neighbor_cell = current_cell->periodic_neighbor(iface);

            	if (!current_cell->periodic_neighbor_is_coarser(iface) &&
            		(neighbor_cell->index() > current_cell->index() ||
            		 (neighbor_cell->index() == current_cell->index() && current_cell->level() < neighbor_cell->level())
            		)
				   )
            	{
            		n_face_visited++;

            		dealii::Vector<double> neighbor_cell_rhs (dofs_per_cell); // Defaults to 0.0 initialization
	                Assert (current_cell->neighbor(iface).state() == dealii::IteratorState::valid, dealii::ExcInternalError());
	                typename dealii::DoFHandler<dim>::cell_iterator neighbor_cell = current_cell->periodic_neighbor(iface);

                    neighbor_cell->get_dof_indices (neighbor_dofs_indices);

                    const unsigned int neighbor_face_no = current_cell->periodic_neighbor_of_periodic_neighbor(iface); //removed const

            	    const unsigned int normal_direction1 = dealii::GeometryInfo<dim>::unit_normal_direction[iface];
            	    const unsigned int normal_direction2 = dealii::GeometryInfo<dim>::unit_normal_direction[neighbor_face_no];
            	    const unsigned int degree_current = DGBase<dim,real>::fe_system.tensor_degree();
            	    const unsigned int deg1sq = (degree_current == 0) ? 1 : degree_current * (degree_current+1);
            	    const unsigned int deg2sq = (degree_current == 0) ? 1 : degree_current * (degree_current+1);

            	    //const real vol_div_facearea1 = current_cell->extent_in_direction(normal_direction1) / current_face->number_of_children();
            	    const real vol_div_facearea1 = current_cell->extent_in_direction(normal_direction1);
            	    const real vol_div_facearea2 = neighbor_cell->extent_in_direction(normal_direction2);
	                const real penalty1 = deg1sq / vol_div_facearea1;
	                const real penalty2 = deg2sq / vol_div_facearea2;

	                real penalty = 0.5 * ( penalty1 + penalty2 );
            		//penalty = 1;//99;

            		fe_values_face_int.reinit (current_cell, iface);
            		fe_values_face_ext.reinit (neighbor_cell, neighbor_face_no);
            		//std::cout << "about to assemble the periodic cell terms!" << std::endl;
            		assemble_face_term_explicit (
            		                        fe_values_face_int, fe_values_face_ext,
            		                        penalty,
            		                        current_dofs_indices, neighbor_dofs_indices,
            		                        current_cell_rhs, neighbor_cell_rhs);
            		//std::cout << "done assembleing the periodic cell terms!" << std::endl;
            		// Add local contribution from neighbor cell to global vector
            		for (unsigned int i=0; i<dofs_per_cell; ++i) {
            			DGBase<dim,real>::right_hand_side(neighbor_dofs_indices[i]) += neighbor_cell_rhs(i);
            	    }
            	}
            	else
            	{
            		//do nothing
            	}

            // Case 2:
            // Neighbour is finer occurs if the face has children
            // This is because we are looping over the current_cell's face, so 2, 4, and 6 faces.
            } else if (current_face->has_children()) {
                //std::cout << "SHOULD NOT HAPPEN!!!!!!!!!!!! I haven't put in adaptatation yet" << std::endl;

                dealii::Vector<double> neighbor_cell_rhs (dofs_per_cell); // Defaults to 0.0 initialization
                Assert (current_cell->neighbor(iface).state() == dealii::IteratorState::valid, dealii::ExcInternalError());

                // Obtain cell neighbour
                const unsigned int neighbor_face_no = current_cell->neighbor_face_no(iface);

                for (unsigned int subface_no=0; subface_no < current_face->number_of_children(); ++subface_no) {

                    n_face_visited++;

                    typename dealii::DoFHandler<dim>::cell_iterator neighbor_child_cell = current_cell->neighbor_child_on_subface (iface, subface_no);

                    Assert (!neighbor_child_cell->has_children(), dealii::ExcInternalError());

                    neighbor_child_cell->get_dof_indices (neighbor_dofs_indices);

                    fe_values_subface_int.reinit (current_cell, iface, subface_no);
                    fe_values_face_ext.reinit (neighbor_child_cell, neighbor_face_no);

                    const unsigned int normal_direction1 = dealii::GeometryInfo<dim>::unit_normal_direction[iface];
                    const unsigned int normal_direction2 = dealii::GeometryInfo<dim>::unit_normal_direction[neighbor_face_no];
                    const unsigned int degree_current = DGBase<dim,real>::fe_system.tensor_degree();
                    const unsigned int deg1sq = (degree_current == 0) ? 1 : degree_current * (degree_current+1);
                    const unsigned int deg2sq = (degree_current == 0) ? 1 : degree_current * (degree_current+1);

                    //const real vol_div_facearea1 = current_cell->extent_in_direction(normal_direction1) / current_face->number_of_children();
                    const real vol_div_facearea1 = current_cell->extent_in_direction(normal_direction1);
                    const real vol_div_facearea2 = neighbor_child_cell->extent_in_direction(normal_direction2);

                    const real penalty1 = deg1sq / vol_div_facearea1;
                    const real penalty2 = deg2sq / vol_div_facearea2;
                    
                    real penalty = 0.5 * ( penalty1 + penalty2 );
                    //penalty = 1;

                    assemble_face_term_explicit (
                        fe_values_subface_int, fe_values_face_ext,
                        penalty,
                        current_dofs_indices, neighbor_dofs_indices,
                        current_cell_rhs, neighbor_cell_rhs);
                    // Add local contribution from neighbor cell to global vector
                    for (unsigned int i=0; i<dofs_per_cell; ++i) {
                        DGBase<dim,real>::right_hand_side(neighbor_dofs_indices[i]) += neighbor_cell_rhs(i);
                    }
                }

            // Case 3:
            // Neighbor cell is NOT coarser
            // Therefore, they have the same coarseness, and we need to choose one of them to do the work
            } else if ( //added a criteria for periodicity
                (!current_cell->neighbor_is_coarser(iface))  &&
                    // Cell with lower index does work
                    (neighbor_cell->index() > current_cell->index() || 
                    // If both cells have same index
                    // See https://www.dealii.org/developer/doxygen/deal.II/classTriaAccessorBase.html#a695efcbe84fefef3e4c93ee7bdb446ad
                    // then cell at the lower level does the work
                        (neighbor_cell->index() == current_cell->index() && current_cell->level() < neighbor_cell->level())
                    ) )
            {
                n_face_visited++;

                dealii::Vector<double> neighbor_cell_rhs (dofs_per_cell); // Defaults to 0.0 initialization

                Assert (current_cell->neighbor(iface).state() == dealii::IteratorState::valid, dealii::ExcInternalError());
                typename dealii::DoFHandler<dim>::cell_iterator neighbor_cell = current_cell->neighbor_or_periodic_neighbor(iface);

                neighbor_cell->get_dof_indices (neighbor_dofs_indices);

                unsigned int neighbor_face_no = current_cell->neighbor_of_neighbor(iface);

                const unsigned int normal_direction1 = dealii::GeometryInfo<dim>::unit_normal_direction[iface];
                const unsigned int normal_direction2 = dealii::GeometryInfo<dim>::unit_normal_direction[neighbor_face_no];
                const unsigned int degree_current = DGBase<dim,real>::fe_system.tensor_degree();
                const unsigned int deg1sq = (degree_current == 0) ? 1 : degree_current * (degree_current+1);
                const unsigned int deg2sq = (degree_current == 0) ? 1 : degree_current * (degree_current+1);

                //const real vol_div_facearea1 = current_cell->extent_in_direction(normal_direction1) / current_face->number_of_children();
                const real vol_div_facearea1 = current_cell->extent_in_direction(normal_direction1);
                const real vol_div_facearea2 = neighbor_cell->extent_in_direction(normal_direction2);

                const real penalty1 = deg1sq / vol_div_facearea1;
                const real penalty2 = deg2sq / vol_div_facearea2;
                
                real penalty = 0.5 * ( penalty1 + penalty2 );
                //penalty = 1;//99;

                fe_values_face_int.reinit (current_cell, iface);
                fe_values_face_ext.reinit (neighbor_cell, neighbor_face_no);
                assemble_face_term_explicit (
                        fe_values_face_int, fe_values_face_ext,
                        penalty,
                        current_dofs_indices, neighbor_dofs_indices,
                        current_cell_rhs, neighbor_cell_rhs);
             //   std::cout << "done assembling the non-periodic cell terms!" << std::endl;
                // Add local contribution from neighbor cell to global vector
                for (unsigned int i=0; i<dofs_per_cell; ++i) {
                    DGBase<dim,real>::right_hand_side(neighbor_dofs_indices[i]) += neighbor_cell_rhs(i);
                }
            } else {
            // Do nothing
            }
            // Case 4: Neighbor is coarser
            // Do nothing.
            // The face contribution from the current cell will appear then the coarse neighbor checks for subfaces

        } // end of face loop

        for (unsigned int i=0; i<dofs_per_cell; ++i) {
            DGBase<dim,real>::right_hand_side(current_dofs_indices[i]) += current_cell_rhs(i);
        }

    } // end of cell loop

} // end of assemble_system_explicit ()


template <int dim, typename real>
void DGBase<dim,real>::assemble_residual_dRdW ()
{
    DGBase<dim,real>::system_matrix = 0;
    DGBase<dim,real>::right_hand_side = 0;

    // For now assume same polynomial degree across domain
    const unsigned int dofs_per_cell = DGBase<dim,real>::dof_handler.get_fe().dofs_per_cell;
    std::vector<dealii::types::global_dof_index> current_dofs_indices (dofs_per_cell);
    std::vector<dealii::types::global_dof_index> neighbor_dofs_indices (dofs_per_cell);

    // ACTIVE cells, therefore, no children
    typename dealii::DoFHandler<dim>::active_cell_iterator
        current_cell = DGBase<dim,real>::dof_handler.begin_active(),
        endc = DGBase<dim,real>::dof_handler.end();

    unsigned int n_cell_visited = 0;
    unsigned int n_face_visited = 0;

    dealii::FEValues<dim,dim>        fe_values_cell (DGBase<dim,real>::mapping, DGBase<dim,real>::fe_system, DGBase<dim,real>::volume_quadrature, this->update_flags);
    dealii::FEFaceValues<dim,dim>    fe_values_face_int (DGBase<dim,real>::mapping, DGBase<dim,real>::fe_system, DGBase<dim,real>::face_quadrature, this->face_update_flags);
    dealii::FESubfaceValues<dim,dim> fe_values_subface_int (DGBase<dim,real>::mapping, DGBase<dim,real>::fe_system, DGBase<dim,real>::face_quadrature, this->face_update_flags);
    dealii::FEFaceValues<dim,dim>    fe_values_face_ext (DGBase<dim,real>::mapping, DGBase<dim,real>::fe_system, DGBase<dim,real>::face_quadrature, this->neighbor_face_update_flags);

    for (; current_cell!=endc; ++current_cell) {
        // std::cout << "Current cell index: " << current_cell->index() << std::endl;
        n_cell_visited++;

        // Local vector contribution from each cell
        dealii::Vector<double> current_cell_rhs (dofs_per_cell); // Defaults to 0.0 initialization

        fe_values_cell.reinit (current_cell);
        current_cell->get_dof_indices (current_dofs_indices);

        assemble_cell_terms_implicit (fe_values_cell, current_dofs_indices, current_cell_rhs);
        for (unsigned int iface=0; iface < dealii::GeometryInfo<dim>::faces_per_cell; ++iface) {

            typename dealii::DoFHandler<dim>::face_iterator current_face = current_cell->face(iface);
            typename dealii::DoFHandler<dim>::cell_iterator neighbor_cell = current_cell->neighbor(iface);

            // See tutorial step-30 for breakdown of 4 face cases

            // Case 1:
            // Face at boundary
            if (current_face->at_boundary()) {

                n_face_visited++;

                fe_values_face_int.reinit (current_cell, iface);

                if(all_parameters->use_periodic_bc == true) //using periodic BCs (for 1d)
                {
                	int cell_index = current_cell->index();
                	if (cell_index == 0 && iface == 0)
                	{
             			fe_values_face_int.reinit(current_cell, iface);
                		typename dealii::DoFHandler<dim>::active_cell_iterator neighbour_cell = dof_handler.begin_active();
                		for (unsigned int i = 0 ; i < triangulation->n_active_cells() - 1; ++i)
                		{
                			++neighbour_cell;
                		}
                		neighbour_cell->get_dof_indices(neighbor_dofs_indices);
                		fe_values_face_ext.reinit(neighbour_cell,(iface == 1) ? 0 : 1);

                	}
                	else if (cell_index == (int) triangulation->n_active_cells() - 1 && iface == 1)
                	{
                		fe_values_face_int.reinit(current_cell, iface);
                		typename dealii::DoFHandler<dim>::active_cell_iterator neighbour_cell = dof_handler.begin_active();
                		neighbour_cell->get_dof_indices(neighbor_dofs_indices);
                		fe_values_face_ext.reinit(neighbour_cell,(iface == 1) ? 0 : 1); //not sure how changing the face number would work in dim!=1-dimensions.
                	}
                }

                else
                {
					const unsigned int degree_current = DGBase<dim,real>::fe_system.tensor_degree();
					const unsigned int deg1sq = (degree_current == 0) ? 1 : degree_current * (degree_current+1);
					const unsigned int normal_direction = dealii::GeometryInfo<dim>::unit_normal_direction[iface];
					const real vol_div_facearea1 = current_cell->extent_in_direction(normal_direction);

					real penalty = deg1sq / vol_div_facearea1;
					//penalty = 1;//99;

					const unsigned int boundary_id = current_face->boundary_id();
					// Need to somehow get boundary type from the mesh

					assemble_boundary_term_implicit (boundary_id, fe_values_face_int, penalty, current_dofs_indices, current_cell_rhs);
                }
            // Case 2:
            // Neighbour is finer occurs if the face has children
            // This is because we are looping over the current_cell's face, so 2, 4, and 6 faces.
            } else if (current_face->has_children()) {
                //std::cout << "SHOULD NOT HAPPEN!!!!!!!!!!!! I haven't put in adaptatation yet" << std::endl;

                dealii::Vector<double> neighbor_cell_rhs (dofs_per_cell); // Defaults to 0.0 initialization
                Assert (current_cell->neighbor(iface).state() == dealii::IteratorState::valid, dealii::ExcInternalError());

                // Obtain cell neighbour
                const unsigned int neighbor_face_no = current_cell->neighbor_face_no(iface);

                for (unsigned int subface_no=0; subface_no < current_face->number_of_children(); ++subface_no) {

                    n_face_visited++;

                    typename dealii::DoFHandler<dim>::cell_iterator neighbor_child_cell = current_cell->neighbor_child_on_subface (iface, subface_no);

                    Assert (!neighbor_child_cell->has_children(), dealii::ExcInternalError());

                    neighbor_child_cell->get_dof_indices (neighbor_dofs_indices);

                    fe_values_subface_int.reinit (current_cell, iface, subface_no);
                    fe_values_face_ext.reinit (neighbor_child_cell, neighbor_face_no);

                    const unsigned int normal_direction1 = dealii::GeometryInfo<dim>::unit_normal_direction[iface];
                    const unsigned int normal_direction2 = dealii::GeometryInfo<dim>::unit_normal_direction[neighbor_face_no];
                    const unsigned int degree_current = DGBase<dim,real>::fe_system.tensor_degree();
                    const unsigned int deg1sq = (degree_current == 0) ? 1 : degree_current * (degree_current+1);
                    const unsigned int deg2sq = (degree_current == 0) ? 1 : degree_current * (degree_current+1);

                    //const real vol_div_facearea1 = current_cell->extent_in_direction(normal_direction1) / current_face->number_of_children();
                    const real vol_div_facearea1 = current_cell->extent_in_direction(normal_direction1);
                    const real vol_div_facearea2 = neighbor_child_cell->extent_in_direction(normal_direction2);

                    const real penalty1 = deg1sq / vol_div_facearea1;
                    const real penalty2 = deg2sq / vol_div_facearea2;
                    
                    real penalty = 0.5 * ( penalty1 + penalty2 );
                    //penalty = 1;

                    assemble_face_term_implicit (
                        fe_values_subface_int, fe_values_face_ext,
                        penalty,
                        current_dofs_indices, neighbor_dofs_indices,
                        current_cell_rhs, neighbor_cell_rhs);

                    // Add local contribution from neighbor cell to global vector
                    for (unsigned int i=0; i<dofs_per_cell; ++i) {
                        DGBase<dim,real>::right_hand_side(neighbor_dofs_indices[i]) += neighbor_cell_rhs(i);
                    }
                }

            // Case 3:
            // Neighbor cell is NOT coarser
            // Therefore, they have the same coarseness, and we need to choose one of them to do the work
            } else if (
                !current_cell->neighbor_is_coarser(iface) &&
                    // Cell with lower index does work
                    (neighbor_cell->index() > current_cell->index() || 
                    // If both cells have same index
                    // See https://www.dealii.org/developer/doxygen/deal.II/classTriaAccessorBase.html#a695efcbe84fefef3e4c93ee7bdb446ad
                    // then cell at the lower level does the work
                        (neighbor_cell->index() == current_cell->index() && current_cell->level() < neighbor_cell->level())
                    ) )
            {
                n_face_visited++;

                dealii::Vector<double> neighbor_cell_rhs (dofs_per_cell); // Defaults to 0.0 initialization

                Assert (current_cell->neighbor(iface).state() == dealii::IteratorState::valid, dealii::ExcInternalError());
                typename dealii::DoFHandler<dim>::cell_iterator neighbor_cell = current_cell->neighbor(iface);

                neighbor_cell->get_dof_indices (neighbor_dofs_indices);

                const unsigned int neighbor_face_no = current_cell->neighbor_of_neighbor(iface);

                const unsigned int normal_direction1 = dealii::GeometryInfo<dim>::unit_normal_direction[iface];
                const unsigned int normal_direction2 = dealii::GeometryInfo<dim>::unit_normal_direction[neighbor_face_no];
                const unsigned int degree_current = DGBase<dim,real>::fe_system.tensor_degree();
                const unsigned int deg1sq = (degree_current == 0) ? 1 : degree_current * (degree_current+1);
                const unsigned int deg2sq = (degree_current == 0) ? 1 : degree_current * (degree_current+1);

                //const real vol_div_facearea1 = current_cell->extent_in_direction(normal_direction1) / current_face->number_of_children();
                const real vol_div_facearea1 = current_cell->extent_in_direction(normal_direction1);
                const real vol_div_facearea2 = neighbor_cell->extent_in_direction(normal_direction2);

                const real penalty1 = deg1sq / vol_div_facearea1;
                const real penalty2 = deg2sq / vol_div_facearea2;
                
                real penalty = 0.5 * ( penalty1 + penalty2 );
                //penalty = 1;//99;

                fe_values_face_int.reinit (current_cell, iface);
                fe_values_face_ext.reinit (neighbor_cell, neighbor_face_no);
                assemble_face_term_implicit (
                        fe_values_face_int, fe_values_face_ext,
                        penalty,
                        current_dofs_indices, neighbor_dofs_indices,
                        current_cell_rhs, neighbor_cell_rhs);

                // Add local contribution from neighbor cell to global vector
                for (unsigned int i=0; i<dofs_per_cell; ++i) {
                    DGBase<dim,real>::right_hand_side(neighbor_dofs_indices[i]) += neighbor_cell_rhs(i);
                }
            } else {
            // Do nothing
            }
            // Case 4: Neighbor is coarser
            // Do nothing.
            // The face contribution from the current cell will appear then the coarse neighbor checks for subfaces

        } // end of face loop

        for (unsigned int i=0; i<dofs_per_cell; ++i) {
            DGBase<dim,real>::right_hand_side(current_dofs_indices[i]) += current_cell_rhs(i);
        }

    } // end of cell loop

} // end of assemble_system_implicit ()


template <int dim, typename real>
double DGBase<dim,real>::get_residual_l2norm ()
{
    return DGBase<dim,real>::right_hand_side.l2_norm();
}

template <int dim, typename real>
void DGBase<dim,real>::output_results (const unsigned int ith_grid)// const
{
    const std::string filename = "sol-" +
                                 dealii::Utilities::int_to_string(ith_grid,2) +
                                 ".gnuplot";
  
    std::cout << "Writing solution to <" << filename << ">..."
              << std::endl << std::endl;
    std::ofstream gnuplot_output (filename.c_str());
  
    dealii::DataOut<dim> data_out;
    data_out.attach_dof_handler (dof_handler);
    data_out.add_data_vector (solution, "u", dealii::DataOut<dim>::type_dof_data);
  
    data_out.build_patches (mapping, fe_system.tensor_degree()+1, dealii::DataOut<dim>::curved_inner_cells);
  
    data_out.write_gnuplot(gnuplot_output);
}

template <int dim, typename real>
void DGBase<dim,real>::output_results_vtk (const unsigned int ith_grid)// const
{




    //std::vector<std::string> solution_names;
    //for(int s=0;s<nstate;++s) {
    //    std::string varname = "u" + dealii::Utilities::int_to_string(s,1);
    //    solution_names.push_back(varname);
    //}
    //std::vector<dealii::DataComponentInterpretation::DataComponentInterpretation> data_component_interpretation(nstate, dealii::DataComponentInterpretation::component_is_scalar);
    //data_out.add_data_vector (solution, solution_names, dealii::DataOut<dim>::type_dof_data, data_component_interpretation);

    const std::unique_ptr< dealii::DataPostprocessor<dim> > post_processor = Postprocess::PostprocessorFactory<dim>::create_Postprocessor(all_parameters);
    dealii::DataOut<dim> data_out;
    data_out.attach_dof_handler (dof_handler);
    data_out.add_data_vector (solution, *post_processor);

    data_out.build_patches (mapping, fe_system.tensor_degree()+1, dealii::DataOut<dim>::curved_inner_cells);
    std::string filename = "solution-" +dealii::Utilities::int_to_string(dim, 1) +"D-"+ dealii::Utilities::int_to_string(ith_grid, 3) + ".vtk";
    std::ofstream output(filename);
    data_out.write_vtk(output);
}


template <int dim, typename real>
void DGBase<dim,real>::evaluate_mass_matrices (bool do_inverse_mass_matrix)
{
    unsigned int n_dofs = dof_handler.n_dofs();
    const int n_dofs_per_cell    = fe_system.dofs_per_cell;
    //const dealii::SparsityPattern sp(n_dofs, n_dofs_per_cell);
    dealii::TrilinosWrappers::SparsityPattern sp(n_dofs, n_dofs, n_dofs_per_cell);
    dealii::DoFTools::make_sparsity_pattern(dof_handler, sp);
    sp.compress();

    if (do_inverse_mass_matrix == true) {
        global_inverse_mass_matrix.reinit(sp);
    } else {
        global_mass_matrix.reinit(sp);
    }

    const int n_quad_pts      = volume_quadrature.size();

    typename dealii::DoFHandler<dim>::active_cell_iterator
       cell = dof_handler.begin_active(),
       endc = dof_handler.end();

    dealii::FullMatrix<real> local_mass_matrix(n_dofs_per_cell);
    dealii::FullMatrix<real> local_inverse_mass_matrix(n_dofs_per_cell);
    std::vector<dealii::types::global_dof_index> dofs_indices (n_dofs_per_cell);
    dealii::FEValues<dim,dim> fe_values_cell (DGBase<dim,real>::mapping, DGBase<dim,real>::fe_system, DGBase<dim,real>::volume_quadrature, this->update_flags);
    for (; cell!=endc; ++cell) {

        //int cell_index = cell->index();
        cell->get_dof_indices (dofs_indices);
        fe_values_cell.reinit(cell);

        for (int itest=0; itest<n_dofs_per_cell; ++itest) {
            const unsigned int istate_test = fe_values_cell.get_fe().system_to_component_index(itest).first;
            for (int itrial=itest; itrial<n_dofs_per_cell; ++itrial) {
                const unsigned int istate_trial = fe_values_cell.get_fe().system_to_component_index(itrial).first;
                real value = 0.0;
                for (int iquad=0; iquad<n_quad_pts; ++iquad) {
                    value +=
                        fe_values_cell.shape_value_component(itest,iquad,istate_test)
                        * fe_values_cell.shape_value_component(itrial,iquad,istate_trial)
                        * fe_values_cell.JxW(iquad);
                }
                local_mass_matrix[itrial][itest] = 0.0;
                local_mass_matrix[itest][itrial] = 0.0;
                if(istate_test==istate_trial) { 
                    local_mass_matrix[itrial][itest] = value;
                    local_mass_matrix[itest][itrial] = value;
                }
            }
        }
        if (do_inverse_mass_matrix == true) {
            local_inverse_mass_matrix.invert(local_mass_matrix);
            global_inverse_mass_matrix.set (dofs_indices, local_inverse_mass_matrix);
        } else {
            global_mass_matrix.set (dofs_indices, local_mass_matrix);
        }
    }

    if (do_inverse_mass_matrix == true) {
        global_inverse_mass_matrix.compress(dealii::VectorOperation::insert);
    } else {
        global_mass_matrix.compress(dealii::VectorOperation::insert);
    }

    return;
}
template<int dim, typename real>
void DGBase<dim,real>::add_mass_matrices(const real scale)
{
    system_matrix.add(scale, global_mass_matrix);
}

template <int dim, typename real>
std::vector<real> DGBase<dim,real>::evaluate_time_steps (const bool exact_time_stepping)
{
    // TO BE DONE
    std::vector<real> time_steps(10);
    if(exact_time_stepping) return time_steps;
    return time_steps;
}

template class DGBase <PHILIP_DIM, double>;
template class DGFactory <PHILIP_DIM, double>;

} // PHiLiP namespace
