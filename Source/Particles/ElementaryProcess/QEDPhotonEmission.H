/* Copyright 2019 Luca Fedeli
 *
 * This file is part of WarpX.
 *
 * License: BSD-3-Clause-LBNL
 */

#ifndef QED_PHOTON_EMISSION_H_
#define QED_PHOTON_EMISSION_H_

#include "Utils/WarpXConst.H"
#include "Particles/WarpXParticleContainer.H"
#include "Particles/Gather/GetExternalFields.H"
#include "Particles/Gather/FieldGather.H"
#include "Particles/Pusher/GetAndSetPosition.H"
#include "QEDInternals/QuantumSyncEngineWrapper.H"

/** @file
 *
 * This file contains the implementation of the elementary process
 * functors needed for QED photon emission (an electron or a positron
 * emits a photon).
 */

/**
 * \brief Filter functor for the QED photon emission process
 */
class PhotonEmissionFilterFunc
{
public:

    /**
    * \brief Constructor of the PhotonEmissionFilterFunc functor.
    *
    * @param[in] opt_depth_runtime_comp Index of the optical depth component
    */
    PhotonEmissionFilterFunc(int const opt_depth_runtime_comp)
        : m_opt_depth_runtime_comp(opt_depth_runtime_comp)
        {}

    /**
    * \brief Functor call. This method determines if a given (electron or positron)
    * particle should undergo QED photon emission.
    *
    * @param[in] ptd particle tile data
    * @param[in] i particle index
    * @return true if a pair has to be generated, false otherwise
    */
    template <typename PData>
    AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    bool operator() (const PData& ptd, int const i) const noexcept
    {
        using namespace amrex;

        const amrex::ParticleReal opt_depth =
            ptd.m_runtime_rdata[m_opt_depth_runtime_comp][i];
        return (opt_depth < 0.0_rt);
    }

private:
    int m_opt_depth_runtime_comp; /*!< Index of the optical depth component of the source species*/
};

/**
 * \brief Transform functor for the QED photon emission process
 */
class PhotonEmissionTransformFunc
{

public:

    /**
    * \brief Constructor of the PhotonEmissionTransformFunc functor.
    *
    * A QuantumSynchrotronGeneratePhotonAndUpdateMomentum functor is passed by value. However, it contains
    * only few integer and real parameters and few pointers to the raw data of the
    * lookup tables. Therefore, it should be rather lightweight to copy.
    *
    * Also a QuantumSynchrotronGetOpticalDepth has to be passed, since the
    * optical depth has to be re-initialized after each photon emission.
    *
    * @param[in] opt_depth_functor functor to re-initialize the optical depth of the source particles
    * @param[in] opt_depth_runtime_comp index of the optical depth component of the source species
    * @param[in] emission_functor functor to generate photons and update momentum of the source particles
    */
    PhotonEmissionTransformFunc (
        QuantumSynchrotronGetOpticalDepth opt_depth_functor,
        int const opt_depth_runtime_comp,
        QuantumSynchrotronGeneratePhotonAndUpdateMomentum const emission_functor,
        const WarpXParIter& a_pti, int lev, int ngE,
        amrex::FArrayBox const& exfab,
        amrex::FArrayBox const& eyfab,
        amrex::FArrayBox const& ezfab,
        amrex::FArrayBox const& bxfab,
        amrex::FArrayBox const& byfab,
        amrex::FArrayBox const& bzfab,
        amrex::Array<amrex::Real,3> v_galilean,
        int a_offset = 0);

    /**
    * \brief Functor call. It determines the properties of the generated photon
    * and updates the momentum of the source particle
    *
    * @param[in,out] dst target species (photons)
    * @param[in, out] src source species (either electrons or positrons)
    * @param[in] i_src particle index of the source species
    * @param[in] i_dst particle index of target species
    */
    template <typename DstData, typename SrcData>
    AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
    void operator() (DstData& dst, SrcData& src, int i_src, int i_dst) const noexcept
    {
        using namespace amrex;

        constexpr ParticleReal me = PhysConst::m_e;
        constexpr ParticleReal one_over_me = 1._prt/me;

        const ParticleReal w = src.m_rdata[PIdx::w][i_src];
        const ParticleReal ux = src.m_rdata[PIdx::ux][i_src];
        const ParticleReal uy = src.m_rdata[PIdx::uy][i_src];
        const ParticleReal uz = src.m_rdata[PIdx::uz][i_src];

        // gather E and B
        amrex::ParticleReal xp, yp, zp;
        m_get_position(i_src, xp, yp, zp);

        amrex::ParticleReal ex = 0._rt, ey = 0._rt, ez = 0._rt;
        m_get_externalE(i_src, ex, ey, ez);

        amrex::ParticleReal bx = 0._rt, by = 0._rt, bz = 0._rt;
        m_get_externalB(i_src, bx, by, bz);

        doGatherShapeN(xp, yp, zp, ex, ey, ez, bx, by, bz,
                       m_ex_arr, m_ey_arr, m_ez_arr, m_bx_arr, m_by_arr, m_bz_arr,
                       m_ex_type, m_ey_type, m_ez_type, m_bx_type, m_by_type, m_bz_type,
                       m_dx_arr, m_xyzmin_arr, m_lo, m_n_rz_azimuthal_modes,
                       m_nox, m_galerkin_interpolation);

        // Particle momentum is stored as gamma * velocity.
        // Convert to m * gamma * velocity before applying the emission functor.
        auto px = ux*me;
        auto py = uy*me;
        auto pz = uz*me;

        auto g_w = 0.0_rt;
        auto g_px = 0.0_rt;
        auto g_py = 0.0_rt;
        auto g_pz = 0.0_rt;

        m_emission_functor.operator()<1>(
            &px, &py, &pz,
            ex, ey, ez,
            bx, by, bz,
            w,
            &g_px, &g_py, &g_pz,
            &g_w);

        // Then convert back to WarpX convention.
        src.m_rdata[PIdx::ux][i_src] = px*one_over_me;
        src.m_rdata[PIdx::uy][i_src] = py*one_over_me;
        src.m_rdata[PIdx::uz][i_src] = pz*one_over_me;

        dst.m_rdata[PIdx::w][i_dst] = g_w;
        dst.m_rdata[PIdx::ux][i_dst] = g_px*one_over_me;
        dst.m_rdata[PIdx::uy][i_dst] = g_py*one_over_me;
        dst.m_rdata[PIdx::uz][i_dst] = g_pz*one_over_me;

        //Initialize the optical depth component of the source species.
        src.m_runtime_rdata[m_opt_depth_runtime_comp][i_src] =
            m_opt_depth_functor();
    }

private:
    const QuantumSynchrotronGetOpticalDepth
        m_opt_depth_functor;  /*!< A copy of the functor to initialize the optical depth of the source species. */
    const QuantumSynchrotronGeneratePhotonAndUpdateMomentum
        m_emission_functor;  /*!< A copy of the functor to generate photons. It contains only pointers to the lookup tables.*/
    const int m_opt_depth_runtime_comp = 0;  /*!< Index of the optical depth component of source species*/

    GetParticlePosition m_get_position;
    GetExternalEField m_get_externalE;
    GetExternalBField m_get_externalB;

    amrex::Array4<const amrex::Real> m_ex_arr;
    amrex::Array4<const amrex::Real> m_ey_arr;
    amrex::Array4<const amrex::Real> m_ez_arr;
    amrex::Array4<const amrex::Real> m_bx_arr;
    amrex::Array4<const amrex::Real> m_by_arr;
    amrex::Array4<const amrex::Real> m_bz_arr;

    amrex::IndexType m_ex_type;
    amrex::IndexType m_ey_type;
    amrex::IndexType m_ez_type;
    amrex::IndexType m_bx_type;
    amrex::IndexType m_by_type;
    amrex::IndexType m_bz_type;

    amrex::GpuArray<amrex::Real, 3> m_dx_arr;
    amrex::GpuArray<amrex::Real, 3> m_xyzmin_arr;

    bool m_galerkin_interpolation;
    int m_nox;
    int m_n_rz_azimuthal_modes;

    amrex::Dim3 m_lo;
};


/**
* \brief Free function to call to remove immediately
* low energy photons by setting their ID to -1.
*
* @tparam PTile particle tile type
* @param[in,out] ptile a particle tile
* @param[in] old_size the old number of particles
* @param[in] num_added the number of photons added to the tile
* @param[in] energy_threshold the energy threshold
*/
template <typename PTile>
void cleanLowEnergyPhotons(
    PTile& ptile,
    const int old_size, const int num_added,
    const amrex::ParticleReal energy_threshold)
{
    auto pp = ptile.GetArrayOfStructs()().data() + old_size;

    const auto& soa = ptile.GetStructOfArrays();
    const auto p_ux = soa.GetRealData(PIdx::ux).data() + old_size;
    const auto  p_uy = soa.GetRealData(PIdx::uy).data() + old_size;
    const auto  p_uz = soa.GetRealData(PIdx::uz).data() + old_size;

    //The square of the energy threshold
    const auto energy_threshold2 = energy_threshold*energy_threshold;

    amrex::ParallelFor(num_added, [=] AMREX_GPU_DEVICE (int ip) noexcept
    {
        auto& p = pp[ip];

        const auto ux = p_ux[ip];
        const auto uy = p_uy[ip];
        const auto uz = p_uz[ip];

        //The square of the photon energy (in SI units)
        // ( Particle momentum is stored as gamma * velocity.)
        constexpr amrex::ParticleReal me_c = PhysConst::m_e*PhysConst::c;
        const auto phot_energy2 = (ux*ux + uy*uy + uz*uz)*me_c*me_c;

        if (phot_energy2 < energy_threshold2){
            p.id() = - 1;
        }
    });
}


#endif //QED_PHOTON_EMISSION_H_
