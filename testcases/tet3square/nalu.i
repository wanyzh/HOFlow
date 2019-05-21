# -*- mode: yaml -*-
#
# Example Nalu input file for a heat conduction problem
#

Simulations:
  - name: sim1
    time_integrator: ti_1
    optimizer: opt1

linear_solvers:
  - name: solve_scalar
    type: tpetra
    method: gmres 
    preconditioner: sgs 
    tolerance: 1e-3
    max_iterations: 75 
    kspace: 75 
    output_level: 0

realms:

  - name: realm_1
    mesh: tet3square_verylarge.exo
    use_edges: no 
    automatic_decomposition_type: rcb

    equation_systems:
      name: theEqSys
      max_iterations: 2 
  
      solver_system_specification:
        temperature: solve_scalar
   
      systems:
        - HeatConduction:
            name: myHC
            max_iterations: 1
            convergence_tolerance: 1e-5

    initial_conditions:

      - constant: ic_1
        target_name: unspecified-2-triangle
        value:
          temperature: 10.0

    material_properties:
      target_name: unspecified-2-triangle
      specifications:
        - name: density
          type: constant
          value: 1000
        - name: thermal_conductivity
          type: constant
          value: 1.0
        - name: specific_heat
          type: constant
          value: 10

    boundary_conditions:

    - wall_boundary_condition: left
      target_name: left
      wall_user_data:
        heat_flux: 200

    - wall_boundary_condition: right
      target_name: right
      wall_user_data:
        temperature: 20

    - wall_boundary_condition: top
      target_name: top
      wall_user_data:
        adiabatic: true

    - wall_boundary_condition: bottom
      target_name: bottom
      wall_user_data:
        adiabatic: true

    output:
      output_data_base_name: nalu_output.e
      output_frequency: 10
      output_node_set: no 
      output_variables:
       - dual_nodal_volume
       - temperature

Time_Integrators:
  - StandardTimeIntegrator:
      name: ti_1
      start_time: 0
      termination_step_count: 500
      time_step: 10.0 
      time_stepping_type: fixed
      time_step_count: 0
      second_order_accuracy: no

      realms:
        - realm_1