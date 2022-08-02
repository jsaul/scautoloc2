/***************************************************************************
 * Copyright (C) GFZ Potsdam                                               *
 * All rights reserved.                                                    *
 *                                                                         *
 * GNU Affero General Public License Usage                                 *
 * This file may be used under the terms of the GNU Affero                 *
 * Public License version 3.0 as published by the Free Software Foundation *
 * and appearing in the file LICENSE included in the packaging of this     *
 * file. Please review the following information to ensure the GNU Affero  *
 * Public License version 3.0 requirements will be met:                    *
 * https://www.gnu.org/licenses/agpl-3.0.html.                             *
 ***************************************************************************/


#ifndef SEISCOMP_LIBAUTOLOC_UTIL_H_INCLUDED
#define SEISCOMP_LIBAUTOLOC_UTIL_H_INCLUDED

#include <string>
#include <vector>
#include <seiscomp/core/datetime.h>
#include <seiscomp/datamodel/pick.h>
#include <seiscomp/seismology/ttt.h>

#include <seiscomp/autoloc/datamodel.h>


namespace Seiscomp {

namespace Autoloc {

// Compute the distance in degrees between two stations on a sphere
double distance(
	const Autoloc::DataModel::Station* s1,
	const Autoloc::DataModel::Station* s2);

// Compute the distance and azimuths in degrees between two points on a sphere
void delazi(
	double lat1, double lon1,
	double lat2, double lon2,
	double &delta, double &az1, double &az2);

// Compute the distance and azimuths in degrees between a hypocenter and a
// station on a sphere.
void delazi(
	const Autoloc::DataModel::Hypocenter*,
	const Autoloc::DataModel::Station*,
	double &delta, double &az1, double &az2);

// Compute the origin score. The higher the better.
double originScore(
	const Autoloc::DataModel::Origin *origin,
	double maxRMS=3.5, double radius=0.);

// Compute primary and secondary azimuthal gaps for the given origin
bool determineAzimuthalGaps(
	const Autoloc::DataModel::Origin*,
	double *primary,
	double *secondary);

// Various formatters to generate debug output for scautoloc
std::string printDetailed(const Autoloc::DataModel::Origin*);
std::string printOneliner(const Autoloc::DataModel::Origin*);
std::string printOrigin(const Autoloc::DataModel::Origin *origin, bool=false);

double meandev(const Autoloc::DataModel::Origin* origin);

double avgfn(double x);

// Compute the P travel time between two points on a spherical Earth.
typedef Seiscomp::TravelTime TravelTime;
bool travelTimeP (
	double lat1, double lon1, double dep1,
	double lat2, double lon2, double alt2,
	TravelTime&);

bool travelTimeP (
	const Autoloc::DataModel::Hypocenter*,
	const Autoloc::DataModel::Station*,
	TravelTime&);

bool travelTime (
	double lat1, double lon1, double dep1,
	double lat2, double lon2, double alt2,
	const std::string &phase,
	TravelTime&);

bool travelTime (
	const Autoloc::DataModel::Hypocenter*,
	const Autoloc::DataModel::Station*,
	const std::string &phase,
	TravelTime&);


// Format an Autoloc::DataModel::Time time as time stamp.
std::string time2str(const Autoloc::DataModel::Time &t);

// Convert an Autoloc::DataModel::Time time to a Seiscomp::Core::Time
Seiscomp::Core::Time sctime(const Autoloc::DataModel::Time &time);

// Return the Autoloc pick status from a Seiscomp::DataModel::Pick
Autoloc::DataModel::Pick::Status status(const Seiscomp::DataModel::Pick *pick);

// Return true if the location code is "empty"
bool emptyLocationCode(const std::string &locationCode);


bool valid(const Autoloc::DataModel::Pick *pick);

bool manual(const Seiscomp::DataModel::Origin *origin);

int arrivalWithLargestResidual(const Autoloc::DataModel::Origin *origin);

} // namespace Autoloc


namespace Math {
namespace Statistics {

// Compute the RMS of the values in the given vector after subtracting the
// given offset.
double rms(const std::vector<double> &v, double offset = 0);

} // namespace Statistics
} // namespace Math

} // namespace Seiscomp

#endif
