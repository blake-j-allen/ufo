/*
 * (C) Crown Copyright 2024, Met Office
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 */

#include <string>
#include <vector>

#include "ufo/operators/sfccorrected/EvalSurfaceTemperature.h"

#include "eckit/exception/Exceptions.h"
#include "ioda/ObsSpace.h"
#include "oops/util/Logger.h"
#include "ufo/GeoVaLs.h"
#include "ufo/utils/Constants.h"
#include "ufo/utils/VertInterp.interface.h"

namespace ufo {

namespace {
SurfaceOperatorMaker<airTemperatureAt2M_WRFDA> makerT2M_WRFDA_("airTemperatureAt2M_WRFDA");
SurfaceOperatorMaker<airTemperatureAt2M_UKMO> makerT2M_UKMO_("airTemperatureAt2M_UKMO");
SurfaceOperatorMaker<airTemperatureAt2M_GSL> makerT2M_GSL_("airTemperatureAt2M_GSL");
}  // namespace

// ----------------------------------------
// Temperature operator using WRFDA method
// ----------------------------------------
airTemperatureAt2M_WRFDA::airTemperatureAt2M_WRFDA(const std::string & name,
                                                   const Parameters_ & params)
    : SurfaceOperatorBase(name, params)
{
  oops::Variables vars;
  vars.push_back(oops::Variable(params_.geovarGeomZ.value()));
  vars.push_back(oops::Variable(params_.geovarSfcGeomZ.value()));
  vars.push_back(oops::Variable("air_temperature"));
  requiredVars_ += vars;
}

void airTemperatureAt2M_WRFDA::simobs(const ufo::GeoVaLs & gv,
                                      const ioda::ObsSpace & obsdb,
                                      std::vector<float> & hofx) const {
  oops::Log::trace() << "airTemperatureAt2M_WRFDA::simobs starting" << std::endl;

  // Setup parameters used throughout
  const size_t nobs = obsdb.nlocs();
  const float missing = util::missingValue<float>();

  // Create arrays needed
  std::vector<float> model_height_level1(nobs), model_height_surface(nobs),
                     model_T_level1(nobs), obs_height(nobs);

  // Get level 1 height.  If geopotential then convert to geometric height.
  const oops::Variable geomz_var = oops::Variable(params_.geovarGeomZ.value());
  const int surface_level_index = gv.nlevs(geomz_var) - 1;
  gv.getAtLevel(model_height_level1, geomz_var, surface_level_index);
  if (params_.geovarGeomZ.value().find("geopotential") != std::string::npos) {
      oops::Log::trace()  << "ObsSfcCorrected::simulateObs do geopotential conversion profile"
                         << std::endl;
  }

  // Get surface height.  If geopotential then convert to geometric height.
  gv.get(model_height_surface, oops::Variable(params_.geovarSfcGeomZ.value()));
  if (params_.geovarSfcGeomZ.value().find("geopotential") != std::string::npos) {
      oops::Log::trace()  << "ObsSfcCorrected::simulateObs do geopotential conversion surface"
                         << std::endl;
  }

  // Read other data in
  oops::Variable model_T_var = oops::Variable("air_temperature");
  gv.getAtLevel(model_T_level1, model_T_var, surface_level_index);
  obsdb.get_db("MetaData", params_.obsHeightName.value(), obs_height);

  // Loop to calculate hofx
  std::vector<float> model_T_surface(nobs);
  for (size_t iloc = 0; iloc < nobs; ++iloc) {
    hofx[iloc] = missing;
    if (obs_height[iloc] != missing && model_T_level1[iloc] != missing &&
        model_height_level1[iloc] != missing && model_height_surface[iloc] != missing) {
      // Find model surface T using lowest T in model temperature profile
      model_T_surface[iloc] = model_T_level1[iloc] +
              ufo::Constants::Lclr * (model_height_level1[iloc] - model_height_surface[iloc]);
      // Correct to observation height
      hofx[iloc] = model_T_surface[iloc] +
                   ufo::Constants::Lclr * (model_height_surface[iloc] - obs_height[iloc]);
    }
  }

  oops::Log::trace() << "airTemperatureAt2M_WRFDA::simobs complete" << std::endl;
}

void airTemperatureAt2M_WRFDA::settraj() const {
  throw eckit::Exception("airTemperatureAt2M_WRFDA::settraj not yet implemented");
}

void airTemperatureAt2M_WRFDA::TL() const {
  throw eckit::Exception("airTemperatureAt2M_WRFDA::TL not yet implemented");
}

void airTemperatureAt2M_WRFDA::AD() const {
  throw eckit::Exception("airTemperatureAt2M_WRFDA::AD not yet implemented");
}

// ----------------------------------------
// Temperature operator using UKMO method
// ----------------------------------------

airTemperatureAt2M_UKMO::airTemperatureAt2M_UKMO(const std::string & name,
                                                 const Parameters_ & params)
  : SurfaceOperatorBase(name, params)
{
  oops::Variables vars;
  vars.push_back(oops::Variable(params_.geovarGeomZ.value()));
  vars.push_back(oops::Variable(params_.geovarSfcGeomZ.value()));
  vars.push_back(oops::Variable("air_temperature"));
  vars.push_back(oops::Variable("air_pressure"));
  vars.push_back(oops::Variable("air_pressure_at_surface"));
  requiredVars_ += vars;
}

void airTemperatureAt2M_UKMO::simobs(const ufo::GeoVaLs & gv,
                                     const ioda::ObsSpace & obsdb,
                                     std::vector<float> & hofx) const {
  oops::Log::trace() << "airTemperatureAt2M_UKMO::simobs starting" << std::endl;

  // Create oops::Variable needed
  const oops::Variable model_height_var = oops::Variable(params_.geovarGeomZ.value());
  const oops::Variable model_p_var = oops::Variable("air_pressure");
  const oops::Variable model_p_surface_var = oops::Variable("air_pressure_at_surface");
  const oops::Variable model_T_var = oops::Variable("air_temperature");

  // Setup parameters used throughout
  const size_t nobs = obsdb.nlocs();
  const float missing = util::missingValue<float>();
  const double height_used = 2000.0;
  const int model_nlevs = gv.nlevs(model_p_var);
  const double T_exponent = ufo::Constants::rd * ufo::Constants::Lclr / ufo::Constants::grav;

  // Create arrays needed
  std::vector<float> model_height_surface(nobs), model_p_surface(nobs),
      obs_height(nobs);
  std::vector<double> model_p_2000m(nobs), model_T_2000m(nobs);

  // Get level 1 height.  If geopotential then convert to geometric height.
  if (params_.geovarGeomZ.value().find("geopotential") != std::string::npos) {
    oops::Log::trace()  << "ObsSfcCorrected::simulateObs do geopotential conversion profile"
                       << std::endl;
  }

  // Get surface height.  If geopotential then convert to geometric height.
  gv.get(model_height_surface, oops::Variable(params_.geovarSfcGeomZ.value()));
  if (params_.geovarSfcGeomZ.value().find("geopotential") != std::string::npos) {
    oops::Log::trace()  << "ObsSfcCorrected::simulateObs do geopotential conversion surface"
                       << std::endl;
  }

  // Read data in
  gv.get(model_p_surface, model_p_surface_var);
  obsdb.get_db("MetaData", params_.obsHeightName.value(), obs_height);

  // Loop to calculate hofx
  double model_T_surface;
  std::vector<double> profile_height(model_nlevs);
  std::vector<double> profile_pressure(model_nlevs);
  std::vector<double> profile_T(model_nlevs);
  int index = 0;
  double weight = 0.0;
  for (size_t iloc = 0; iloc < nobs; ++iloc) {
    hofx[iloc] = missing;
    if (obs_height[iloc] != missing && model_p_surface[iloc] != missing &&
        model_height_surface[iloc] != missing) {
      // Get model data at this location
      gv.getAtLocation(profile_height, model_height_var, iloc);
      gv.getAtLocation(profile_pressure, model_p_var, iloc);
      gv.getAtLocation(profile_T, model_T_var, iloc);
      // Vertical interpolation to get model pressure and temperature at 2000 m
      vert_interp_weights_f90(model_nlevs, height_used, profile_height.data(), index, weight);
      vert_interp_apply_f90(model_nlevs, profile_pressure.data(),
                            model_p_2000m[iloc], index, weight);
      vert_interp_apply_f90(model_nlevs, profile_T.data(),
                            model_T_2000m[iloc], index, weight);
      // Find model surface T using lowest T in model temperature profile
      model_T_surface = model_T_2000m[iloc] *
        std::pow((model_p_surface[iloc] / model_p_2000m[iloc]), T_exponent);
      // Correct to observation height
      hofx[iloc] = model_T_surface +
        ufo::Constants::Lclr * (model_height_surface[iloc] - obs_height[iloc]);
    }
  }
  oops::Log::trace() << "airTemperatureAt2M_UKMO::simobs complete" << std::endl;
}

void airTemperatureAt2M_UKMO::settraj() const {
  throw eckit::Exception("airTemperatureAt2M_UKMO::settraj not yet implemented");
}

void airTemperatureAt2M_UKMO::TL() const {
  throw eckit::Exception("airTemperatureAt2M_UKMO::TL not yet implemented");
}

void airTemperatureAt2M_UKMO::AD() const {
  throw eckit::Exception("airTemperatureAt2M_UKMO::AD not yet implemented");
}

// ----------------------------------------
// Temperature operator using GSL method
// ----------------------------------------

airTemperatureAt2M_GSL::airTemperatureAt2M_GSL(const std::string & name,
                                               const Parameters_ & params)
  : SurfaceOperatorBase(name, params)
{
  throw eckit::Exception("airTemperatureAt2M_GSL not yet implemented");
}

void airTemperatureAt2M_GSL::simobs(const ufo::GeoVaLs & gv,
                                    const ioda::ObsSpace & obsdb,
                                    std::vector<float> & hofx) const {
  throw eckit::Exception("airTemperatureAt2M_GSL::simobs not yet implemented");
}

void airTemperatureAt2M_GSL::settraj() const {
  throw eckit::Exception("airTemperatureAt2M_GSL::settraj not yet implemented");
}

void airTemperatureAt2M_GSL::TL() const {
  throw eckit::Exception("airTemperatureAt2M_GSL::TL not yet implemented");
}

void airTemperatureAt2M_GSL::AD() const {
  throw eckit::Exception("airTemperatureAt2M_GSL::AD not yet implemented");
}

}  // namespace ufo
