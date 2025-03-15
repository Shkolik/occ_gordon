/*
* SPDX-License-Identifier: Apache-2.0
* SPDX-FileCopyrightText: 2018 German Aerospace Center (DLR)
*
* Created: 2018-05-23 Martin Siggel <Martin.Siggel@dlr.de>
*                with Merlin Pelz   <Merlin.Pelz@dlr.de>
*/

#include "InterpolateCurveNetwork.h"

#include "internal/Error.h"

#include "BSplineAlgorithms.h"
#include "CurveNetworkSorter.h"
#include "GordonSurfaceBuilder.h"

#include <math_Matrix.hxx>
#include <TColStd_HArray1OfReal.hxx>
#include <GeomConvert.hxx>

#include <algorithm>


namespace occ_gordon_internal
{

template <class T>
T Clamp(T val, T min, T max)
{
    if (min > max) {
        throw error("Minimum may not be larger than maximum in clamp!");
    }

    return std::max(min, std::min(val, max));
}

InterpolateCurveNetwork::InterpolateCurveNetwork(const std::vector<Handle (Geom_Curve)> &profiles,
                                                           const std::vector<Handle (Geom_Curve)> &guides,
                                                           double spatialTol)
    : m_hasPerformed(false)
    , m_spatialTol(spatialTol)
{
    // check whether there are any u-directional and v-directional B-splines in the vectors
    if (profiles.size() < 2) {
        throw error("There must be at least two profiles for the curve network interpolation.", MATH_ERROR);
    }

    if (guides.size()  < 2) {
        throw error("There must be at least two guides for the curve network interpolation.", MATH_ERROR);
    }
    
    m_profiles.reserve(profiles.size());
    m_guides.reserve(guides.size());

    // Copy the curves
    for (std::vector<Handle (Geom_Curve)>::const_iterator it = profiles.begin(); it != profiles.end(); ++it) {
        m_profiles.push_back(GeomConvert::CurveToBSplineCurve(*it));
    }
    for (std::vector<Handle (Geom_Curve)>::const_iterator it = guides.begin(); it != guides.end(); ++it) {
        m_guides.push_back(GeomConvert::CurveToBSplineCurve(*it));
    }
}


void InterpolateCurveNetwork::ComputeIntersections(math_Matrix& intersection_params_u,
                                                        math_Matrix& intersection_params_v) const
{
    const std::vector<Handle(Geom_BSplineCurve)>& profiles = m_profiles;
    const std::vector<Handle(Geom_BSplineCurve)>& guides = m_guides;
    
    for (int spline_u_idx = 0; spline_u_idx < static_cast<int>(profiles.size()); ++spline_u_idx) {
        for (int spline_v_idx = 0; spline_v_idx < static_cast<int>(guides.size()); ++spline_v_idx) {
            std::vector<std::pair<double, double> > currentIntersections = BSplineAlgorithms::intersections(profiles[static_cast<size_t>(spline_u_idx)],
                                                                                                                 guides[static_cast<size_t>(spline_v_idx)],
                                                                                                                 m_spatialTol);

            if (currentIntersections.size() < 1) {
                throw error("U-directional B-spline and v-directional B-spline don't intersect each other!");
            }

            else if (currentIntersections.size() == 1) {
                intersection_params_u(spline_u_idx, spline_v_idx) = currentIntersections[0].first;
                intersection_params_v(spline_u_idx, spline_v_idx) = currentIntersections[0].second;
            }
            else if (currentIntersections.size() == 2) {
                // NB: We couldn't handle closed curves here, because we did't know vhat profile/guide intesect in lowest parameter and 
                // there is no way to let API user know it upfront. So we will handle it later
                // Commented code below was working fine anly if curves passed to API were already sorte in correct order 
                // and first curve in profiles was intersecting guide in a smallest parameter value
            }
            /*

                // for closed curves
            else if (currentIntersections.size() == 2) {

                // only the u-directional B-spline curves are closed
                if (profiles[0]->IsClosed()) {

                    if (spline_v_idx == 0) {
                        intersection_params_u(spline_u_idx, spline_v_idx) = std::min(currentIntersections[0].first, currentIntersections[1].first);
                    }
                    else if (spline_v_idx == static_cast<int>(guides.size() - 1)) {
                        intersection_params_u(spline_u_idx, spline_v_idx) = std::max(currentIntersections[0].first, currentIntersections[1].first);
                    }

                    // intersection_params_vector[0].second == intersection_params_vector[1].second
                    intersection_params_v(spline_u_idx, spline_v_idx) = currentIntersections[0].second;
                }

                // only the v-directional B-spline curves are closed
                if (guides[0]->IsClosed()) {

                    if (spline_u_idx == 0) {
                        intersection_params_v(spline_u_idx, spline_v_idx) = std::min(currentIntersections[0].second, currentIntersections[1].second);
                    }
                    else if (spline_u_idx == static_cast<int>(profiles.size() - 1)) {
                        intersection_params_v(spline_u_idx, spline_v_idx) = std::max(currentIntersections[0].second, currentIntersections[1].second);
                    }
                    // intersection_params_vector[0].first == intersection_params_vector[1].first
                    intersection_params_u(spline_u_idx, spline_v_idx) = currentIntersections[0].first;
                }

//                // TODO: both u-directional splines and v-directional splines are closed
//               else if (intersection_params_vector.size() == 4) {

//                }
            }

            else if (currentIntersections.size() > 2) {

            */
            else{
                throw error("U-directional B-spline and v-directional B-spline have more than two intersections with each other!");
            }
        }
    }
}

void InterpolateCurveNetwork::SortCurves(math_Matrix& intersection_params_u, math_Matrix& intersection_params_v)
{
    CurveNetworkSorter sorterObj(std::vector<Handle(Geom_Curve)>(m_profiles.begin(), m_profiles.end()),
                                      std::vector<Handle(Geom_Curve)>(m_guides.begin(), m_guides.end()),
                                      intersection_params_u,
                                      intersection_params_v);
    sorterObj.Perform();

    // get the sorted matrices and vectors
    intersection_params_u = sorterObj.ProfileIntersectionParms();
    intersection_params_v = sorterObj.GuideIntersectionParms();

    // copy sorted curves back into our curve arrays
    struct Caster {
        Handle(Geom_BSplineCurve) operator()(const Handle(Geom_Curve)& curve) {
            return Handle(Geom_BSplineCurve)::DownCast(curve);
        }
    } caster;

    std::transform(sorterObj.Profiles().begin(), sorterObj.Profiles().end(), m_profiles.begin(), caster);
    std::transform(sorterObj.Guides().begin(), sorterObj.Guides().end(), m_guides.begin(), caster);
}

void InterpolateCurveNetwork::MakeCurvesCompatible()
{

    // reparametrize into [0,1]
    for (CurveArray::iterator it = m_profiles.begin(); it != m_profiles.end(); ++it) {
        BSplineAlgorithms::reparametrizeBSpline(*(*it), 0., 1., 1e-15);
    }

    for (CurveArray::iterator it = m_guides.begin(); it != m_guides.end(); ++it) {
        BSplineAlgorithms::reparametrizeBSpline(*(*it), 0., 1., 1e-15);
    }
    // now the parameter range of all  profiles and guides is [0, 1]

    int nGuides = static_cast<int>(m_guides.size());
    int nProfiles = static_cast<int>(m_profiles.size());
    // now find all intersections of all B-splines with each other
    math_Matrix tmp_intersection_params_u(0, nProfiles - 1,
                                      0, nGuides - 1);
    math_Matrix tmp_intersection_params_v(0, nProfiles - 1,
                                      0, nGuides - 1);
    // closed profiles/guides should not be handled by this method
    // see details inside
    ComputeIntersections(tmp_intersection_params_u, tmp_intersection_params_v);

    // sort intersection_params_u and intersection_params_v and u-directional and v-directional B-spline curves
    SortCurves(tmp_intersection_params_u, tmp_intersection_params_v);

    // Need to check if profiles are closed curves
    // and if so - duplicate 1st guide at the end of guides array and fix intersection matrix
    // we know that parametrization for all profiles are the same, so it's safe to check only first
    // one
    // Do it here, because now we have sorted collections and we clearly know where first intersection is
    auto isClosedProfile = m_profiles.front()->IsClosed() || m_profiles.front()->IsPeriodic();
    auto isClosedGuides = m_guides.front()->IsClosed() || m_guides.front()->IsPeriodic();

    math_Matrix intersection_params_u(0, isClosedGuides ? nProfiles : nProfiles - 1,
        0, isClosedProfile ? nGuides : nGuides - 1);
    math_Matrix intersection_params_v(0, isClosedGuides ? nProfiles : nProfiles - 1,
        0, isClosedProfile ? nGuides : nGuides - 1);

    if (isClosedProfile) {
        m_guides.push_back(m_guides.front());
        ++nGuides;

        // profiles
        for (int spline_u_idx = 0; spline_u_idx < nProfiles; ++spline_u_idx) {
            // guides
            for (int spline_v_idx = 0; spline_v_idx < nGuides - 1; ++spline_v_idx) {
                intersection_params_u(spline_u_idx, spline_v_idx) =
                    tmp_intersection_params_u(spline_u_idx, spline_v_idx);
                intersection_params_v(spline_u_idx, spline_v_idx) =
                    tmp_intersection_params_v(spline_u_idx, spline_v_idx);
            }
            intersection_params_u(spline_u_idx, nGuides - 1) =
                tmp_intersection_params_u(spline_u_idx, 0) < 1e-5   // intersection at 0.0 should be handled differently
				? 1.0                                               // for algorithm to be working properly
				: tmp_intersection_params_u(spline_u_idx, 0);       // so we just set it to 1.0 if it's 0.0 in other cases we just copy the value
            intersection_params_v(spline_u_idx, nGuides - 1) =
                tmp_intersection_params_v(spline_u_idx, 0);
        }
    }
    else if (isClosedGuides) {
        m_profiles.push_back(m_profiles.front());
        ++nProfiles;

        for (int spline_v_idx = 0; spline_v_idx < nGuides; ++spline_v_idx) {
            for (int spline_u_idx = 0; spline_u_idx < nProfiles - 1; ++spline_u_idx) {
                intersection_params_u(spline_u_idx, spline_v_idx) =
                    tmp_intersection_params_u(spline_u_idx, spline_v_idx);
                intersection_params_v(spline_u_idx, spline_v_idx) =
                    tmp_intersection_params_v(spline_u_idx, spline_v_idx);
            }
            intersection_params_u(nProfiles - 1, spline_v_idx) =
                tmp_intersection_params_u(0, spline_v_idx);
            intersection_params_v(nProfiles - 1, spline_v_idx) =
                tmp_intersection_params_v(0, spline_v_idx) < 1e-5   // intersection at 0.0 should be handled differently
				? 1.0   								            // for algorithm to be working properly  
				: tmp_intersection_params_v(0, spline_v_idx);	    // so we just set it to 1.0 if it's 0.0 in other cases we just copy the value
        }
    }
    else
    {
		// if none of the curves are closed, just copy the intersection matrices
        // if both profiles and guides are closed it's a corner case not supported for now
        intersection_params_u = tmp_intersection_params_u;
        intersection_params_v = tmp_intersection_params_v;
    }

    // eliminate small inaccuracies of the intersection parameters:
    EliminateInaccuraciesNetworkIntersections(m_profiles, m_guides, intersection_params_u, intersection_params_v);

    std::vector<double> newParametersProfiles;
    for (int spline_v_idx = 1; spline_v_idx <= nGuides; ++spline_v_idx) {
        double sum = 0;
        for (int spline_u_idx = 1; spline_u_idx <= nProfiles; ++spline_u_idx) {
            sum += intersection_params_u(spline_u_idx - 1, spline_v_idx - 1);
        }
        newParametersProfiles.push_back(sum / nProfiles);
    }

    std::vector<double> newParametersGuides;
    for (int spline_u_idx = 1; spline_u_idx <= nProfiles; ++spline_u_idx) {
        double sum = 0;
        for (int spline_v_idx = 1; spline_v_idx <= nGuides; ++spline_v_idx) {
            sum += intersection_params_v(spline_u_idx - 1, spline_v_idx - 1);
        }
        newParametersGuides.push_back(sum / nGuides);
    }

    if (newParametersProfiles.front() > 1e-5 || newParametersGuides.front() > 1e-5) {
        throw error("At least one B-splines has no intersection at the beginning.");
    }

    // Get maximum number of control points to figure out detail of spline
    size_t max_cp_u = 0, max_cp_v = 0;
    for(CurveArray::const_iterator it = m_profiles.begin(); it != m_profiles.end(); ++it) {
        max_cp_u = std::max(max_cp_u, static_cast<size_t>((*it)->NbPoles()));
    }
    for(CurveArray::const_iterator it = m_guides.begin(); it != m_guides.end(); ++it) {
        max_cp_v = std::max(max_cp_v, static_cast<size_t>((*it)->NbPoles()));
    }

    // we want to use at least 10 and max 80 control points to be able to reparametrize the geometry properly
    size_t mincp = 10;
    size_t maxcp = 80;

    // since we interpolate the intersections, we cannot use fewer control points than curves
    // We need to add two since we want c2 continuity, which adds two equations
    size_t min_u = std::max(m_guides.size() + 2, mincp);
    size_t min_v = std::max(m_profiles.size() + 2, mincp);

    size_t max_u = std::max(min_u, maxcp);
    size_t max_v = std::max(min_v, maxcp);

    max_cp_u = Clamp(max_cp_u + 10, min_u, max_u);
    max_cp_v = Clamp(max_cp_v + 10, min_v, max_v);

    // reparametrize u-directional B-splines
    for (int spline_u_idx = 0; spline_u_idx < nProfiles; ++spline_u_idx) {

        std::vector<double> oldParametersProfile;
        for (int spline_v_idx = 0; spline_v_idx < nGuides; ++spline_v_idx) {
            oldParametersProfile.push_back(intersection_params_u(spline_u_idx, spline_v_idx));
        }

        // eliminate small inaccuracies at the first knot
        if (std::abs(oldParametersProfile.front()) < 1e-5) {
            oldParametersProfile.front() = 0;
        }

        if (std::abs(newParametersProfiles.front()) < 1e-5) {
            newParametersProfiles.front() = 0;
        }

        // eliminate small inaccuracies at the last knot
        if (std::abs(oldParametersProfile.back() - 1) < 1e-5) {
            oldParametersProfile.back() = 1;
        }

        if (std::abs(newParametersProfiles.back() - 1) < 1e-5) {
            newParametersProfiles.back() = 1;
        }

        Handle(Geom_BSplineCurve)& profile = m_profiles[static_cast<size_t>(spline_u_idx)];
        profile = BSplineAlgorithms::reparametrizeBSplineContinuouslyApprox(profile, oldParametersProfile, newParametersProfiles, max_cp_u).curve;
    }

    // reparametrize v-directional B-splines
    for (int spline_v_idx = 0; spline_v_idx < nGuides; ++spline_v_idx) {

        std::vector<double> oldParameterGuide;
        for (int spline_u_idx = 0; spline_u_idx < nProfiles; ++spline_u_idx) {
            oldParameterGuide.push_back(intersection_params_v(spline_u_idx, spline_v_idx));
        }

        // eliminate small inaccuracies at the first knot
        if (std::abs(oldParameterGuide.front()) < 1e-5) {
            oldParameterGuide.front() = 0;
        }

        if (std::abs(newParametersGuides.front()) < 1e-5) {
            newParametersGuides.front() = 0;
        }

        // eliminate small inaccuracies at the last knot
        if (std::abs(oldParameterGuide.back() - 1) < 1e-5) {
            oldParameterGuide.back() = 1;
        }

        if (std::abs(newParametersGuides.back() - 1) < 1e-5) {
            newParametersGuides.back() = 1;
        }

        Handle(Geom_BSplineCurve)& guide = m_guides[static_cast<size_t>(spline_v_idx)];
        guide = BSplineAlgorithms::reparametrizeBSplineContinuouslyApprox(guide, oldParameterGuide, newParametersGuides, max_cp_v).curve;
    }


    m_intersectionParamsU = newParametersProfiles;
    m_intersectionParamsV = newParametersGuides;
}

void InterpolateCurveNetwork::EliminateInaccuraciesNetworkIntersections(const std::vector<Handle(Geom_BSplineCurve)> & sortedProfiles,
                                                                             const std::vector<Handle(Geom_BSplineCurve)> & sortedGuides,
                                                                             math_Matrix & intersection_params_u,
                                                                             math_Matrix & intersection_params_v) const
{
    Standard_Integer nProfiles = static_cast<Standard_Integer>(sortedProfiles.size());
    Standard_Integer nGuides = static_cast<Standard_Integer>(sortedGuides.size());
    // eliminate small inaccuracies of the intersection parameters:

    // first intersection
    for (Standard_Integer spline_u_idx = 0; spline_u_idx < nProfiles; ++spline_u_idx) {
        if (std::abs(intersection_params_u(spline_u_idx, 0) - sortedProfiles[0]->Knot(1)) < 0.001) {
            if (std::abs(sortedProfiles[0]->Knot(1)) < 1e-10) {
                intersection_params_u(spline_u_idx, 0) = 0;
            }
            else {
                intersection_params_u(spline_u_idx, 0) = sortedProfiles[0]->Knot(1);
            }
        }
    }

    for (Standard_Integer spline_v_idx = 0; spline_v_idx < nGuides; ++spline_v_idx) {
        if (std::abs(intersection_params_v(0, spline_v_idx) - sortedGuides[0]->Knot(1)) < 0.001) {
            if (std::abs(sortedGuides[0]->Knot(1)) < 1e-10) {
                intersection_params_v(0, spline_v_idx) = 0;
            }
            else {
                intersection_params_v(0, spline_v_idx) = sortedGuides[0]->Knot(1);
            }
        }
    }

    // last intersection
    for (Standard_Integer spline_u_idx = 0; spline_u_idx < nProfiles; ++spline_u_idx) {
        if (std::abs(intersection_params_u(spline_u_idx, nGuides - 1) - sortedProfiles[0]->Knot(sortedProfiles[0]->NbKnots())) < 0.001) {
            intersection_params_u(spline_u_idx, nGuides - 1) = sortedProfiles[0]->Knot(sortedProfiles[0]->NbKnots());
        }
    }

    for (Standard_Integer spline_v_idx = 0; spline_v_idx < nGuides; ++spline_v_idx) {
        if (std::abs(intersection_params_v(nProfiles - 1, spline_v_idx) - sortedGuides[0]->Knot(sortedGuides[0]->NbKnots())) < 0.001) {
            intersection_params_v(nProfiles - 1, spline_v_idx) = sortedGuides[0]->Knot(sortedGuides[0]->NbKnots());
        }
    }
}


Handle(Geom_BSplineSurface) InterpolateCurveNetwork::Surface()
{
    Perform();

    return m_gordonSurf;
}

InterpolateCurveNetwork::operator Handle(Geom_BSplineSurface) ()
{
    return Surface();
}

Handle(Geom_BSplineSurface) InterpolateCurveNetwork::SurfaceProfiles()
{
    Perform();

    return m_skinningSurfProfiles;
}

Handle(Geom_BSplineSurface) InterpolateCurveNetwork::SurfaceGuides()
{
    Perform();

    return m_skinningSurfGuides;
}

Handle(Geom_BSplineSurface) InterpolateCurveNetwork::SurfaceIntersections()
{
    Perform();

    return m_tensorProdSurf;
}

std::vector<double> InterpolateCurveNetwork::ParametersProfiles()
{
    Perform();

    return m_intersectionParamsV;
}

std::vector<double> InterpolateCurveNetwork::ParametersGuides()
{
    Perform();

    return m_intersectionParamsU;
}

void InterpolateCurveNetwork::Perform()
{
    if (m_hasPerformed) {
        return;
    }

    // Gordon surfaces are only defined on a compatible curve network
    // We first have to reparametrize the network
    MakeCurvesCompatible();
    
    GordonSurfaceBuilder builder(m_profiles, m_guides, m_intersectionParamsU, m_intersectionParamsV, m_spatialTol);
    m_gordonSurf = builder.SurfaceGordon();
    m_skinningSurfProfiles = builder.SurfaceProfiles();
    m_skinningSurfGuides = builder.SurfaceGuides();
    m_tensorProdSurf = builder.SurfaceIntersections();

    m_hasPerformed = true;
}

Handle(Geom_BSplineSurface) curveNetworkToSurface(const std::vector<Handle (Geom_Curve)> &profiles, const std::vector<Handle (Geom_Curve)> &guides, double tol)
{
    return InterpolateCurveNetwork(profiles, guides, tol).Surface();
}

} // namespace occ_gordon_internal
