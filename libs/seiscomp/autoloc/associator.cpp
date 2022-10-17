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


#define SEISCOMP_COMPONENT Autoloc

#include <seiscomp/autoloc/associator.h>
#include <seiscomp/autoloc/datamodel.h>
#include <seiscomp/autoloc/util.h>

#include <seiscomp/logging/log.h>


namespace Seiscomp {

#define minimumAffinity 0.1





// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Associator::Associator()
{
	_origins = 0;
	_stations = 0;

	// The order of the phases is crucial! TODO: Review!
	_phaseRanges.push_back( PhaseRange("P",       0, 115) );
	_phaseRanges.push_back( PhaseRange("PcP",    25,  55) );
	_phaseRanges.push_back( PhaseRange("ScP",    25,  55) );
	_phaseRanges.push_back( PhaseRange("PP",     60, 160) );
	_phaseRanges.push_back( PhaseRange("PKPbc", 140, 160) );
	_phaseRanges.push_back( PhaseRange("PKPdf",  90, 180) );
	_phaseRanges.push_back( PhaseRange("PKPab", 150, 180) );

	// For the following phases there are no tables in LocSAT!
//	_phaseRanges.push_back( PhaseRange("SKP",   120, 150) );
	_phaseRanges.push_back( PhaseRange("PKKP",   80, 130) );
	_phaseRanges.push_back( PhaseRange("PKiKP" , 30, 120) );
//	_phaseRanges.push_back( PhaseRange("SKKP",  110, 152) );

	// Current behaviour: Don't associate picks from disabled stations.
	// TODO: Make this behaviour configurable.

	associateDisabledStationsToQualifiedOrigin = true;

	// TODO
	// - make the phase ranges configurable
	// - make considerDisabledStations configurable
	// - wider view also involving LocSAT tables
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Associator::~Associator()
{
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void
Associator::setStations(const Autoloc::DataModel::StationMap *stations)
{
	_stations = stations;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void
Associator::setOrigins(const Autoloc::DataModel::OriginVector *origins)
{
	_origins = origins;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void
Associator::setPickPool(const Autoloc::DataModel::PickPool *p)
{
	pickPool = p;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void
Associator::reset()
{
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void
Associator::shutdown()
{
	reset();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Associator::findMatchingPicks(
	const Autoloc::DataModel::Origin *origin,
	AssociationVector &associations) const
{
	using namespace Autoloc;
	using namespace Autoloc::DataModel;

	bool considerDisabledStations =
		associateDisabledStationsToQualifiedOrigin &&
		(imported(origin) || manual(origin));

	for (auto &item: *pickPool) {

		const Pick *pick = item.second.get();

//SEISCOMP_ERROR_S("findMatchingPicks A  " + pick->id());
		if (pick->time < origin->time)
			continue;
		if (pick->time > origin->time + 1500.)
			continue;
//SEISCOMP_ERROR("findMatchingPicks A2 %ld %ld", pick->originID(), origin->id);

		OriginID id = pick->originID();
// vvvv begin purely diagnostic output
		if (id) {
			const Origin *tmp = _origins->find(id);
			if ( ! tmp) {
				SEISCOMP_ERROR(
					"Pick %s associated to non-existing origin %ld",
					pick->id().c_str(), id);
//				continue;
			}

			// pick already associated to this origin?
			if (id == origin->id) {
				SEISCOMP_ERROR(
					"Pick %s already associated to this origin %ld",
					pick->id().c_str(), id);
//				continue;
			}

			// pick already associated to another origin
// TODO: This of course needs to be checked later and if the other origin has
// higher score we cannot steal the pick from it.
			SEISCOMP_ERROR(
				"Pick %s already associated to another origin %ld",
				pick->id().c_str(), id);
			SEISCOMP_ERROR_S(printOneliner(tmp));
		}
		else {
			SEISCOMP_ERROR_S(
				"Pick " + pick->id() + " still unassociated");
		}
// ^^^^ end purely diagnostic output

		bool requiresAmplitude = automatic(pick);
		if (requiresAmplitude && ! hasAmplitude(pick)) {
			SEISCOMP_ERROR_S(
				"Pick " + pick->id() + " missing amplitudes");
			continue;
		}

		const Station *station = pick->station();
		if ( ! station) {
			SEISCOMP_ERROR_S(
				"Station missing for pick " + pick->id());
			continue;
		}

		// We may or may not want to associate picks from
		// stations disabled in the station config.
		if ( ! station->enabled && ! considerDisabledStations)
			continue;

		double delta, az, baz;
		delazi(origin, station, delta, az, baz);

		// Weight residuals at regional distances "a bit" lower
		// This is quite hackish!
		double x = 1 + 0.6*exp(-0.003*delta*delta) +
			       0.5*exp(-0.03*(15-delta)*(15-delta));

		Seiscomp::TravelTimeList
			*ttlist = ttt.compute(
				origin->lat, origin->lon, origin->dep,
				station->lat, station->lon, 0);

		Association best;
		best.affinity = 0;

		// For each pick in the pool we compute a travel time table.
		// Then we try to match predicted and measured arrival times.
		for (const Seiscomp::TravelTime &tt : *ttlist) {
			// We skip this phase if we are out of the interesting
			// range or if the phase was not found by inRange().
			//
			// This may well be within the defined phase range,
			// but e.g. PcP gets so close to P at large distances
			// that we cannot separate PcP from P.
			if ( ! inRange(tt.phase, delta, origin->dep))
				continue;

			Time predicted = origin->time + tt.time;
			double residual { pick->time - predicted };
			double affinity { 0 };
			double norm = 0.1; // TODO: review
			double weighed_residual { residual/x * norm };

			// TODO: REVIEW
			// test if exp(-weighed_residual**2) if better
			affinity = avgfn(weighed_residual);

			std::string phase = tt.phase;
			if (isP(phase))
				// Generally we prefer generic name "P" over
				// "Pg", "Pn", "Pb", "Pdiff" et al. but
				// TODO:  need to take care of picks with
				// non-generic phase label, although in the
				// context of this function that should be no
				// real issue.
				phase = "P";

			// certain phases have lower probability because they
			// have smaller amplitude or immediately follow an
			// arrival with usually larger amplitude. This is an
			// attempt to deal with that but this is experimental.
			double phaseWeight { 1. };
			if (phase == "PKPab" || phase == "PKPdf")
				phaseWeight = 0.5;
			affinity *= phaseWeight;

			if (affinity < minimumAffinity)
				continue;
			Association asso(
				origin, pick, phase,
				residual, affinity);
			asso.distance = delta;
			asso.azimuth = az;
			asso.excluded = Arrival::NotExcluded;
			if (asso.affinity > best.affinity)
				best = asso;
		}
		if (best.affinity > 0)
			associations.push_back(best);

		delete ttlist;
	}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool
Associator::findMatchingOrigins(
	const Autoloc::DataModel::Pick *pick,
	AssociationVector &associations) const
{
	using namespace Autoloc::DataModel;

	const Station *station = pick->station();

	for (auto &origin : *_origins) {

		// An imported origin is treated as if it had a very high
		// score. => Anything can be associated with it.
		double score = origin->imported ? 1000 : origin->score;

		double delta, az, baz;
		Autoloc::delazi(origin.get(), station, delta, az, baz);

		Seiscomp::TravelTimeList
			*ttlist = ttt.compute(
				origin->lat, origin->lon, origin->dep,
			        station->lat, station->lon, 0);

		for (auto &phaseRange: _phaseRanges) {

			TravelTime ttime;
			ttime.time = -1;

			// TODO: make this configurable
			// if (origin->definingPhaseCount() < (phase.code=="P" ? 8 : 30))
			if (score < (phaseRange.code=="P" ? 20 : 50))
				continue;

			if ( ! phaseRange.contains(delta, origin->dep))
				continue;

			double x = 1;

			if (phaseRange.code == "P") {
				// first arrival
				for (auto &tt : *ttlist) {
					ttime = tt;
					break;
				}
				// Weight residuals at regional distances
				// "a bit" lower. TODO: review!
				x = 1 + 0.6*exp(-0.003*delta*delta) +
					0.5*exp(-0.03*(15-delta)*(15-delta));
			}
			else {
				for (auto &tt : *ttlist) {
					if (tt.phase.substr(
						0, phaseRange.code.size()) ==
					    phaseRange.code)
					{
						ttime = tt;
						break;
					}
				}
			}

			if (ttime.time == -1) // phase not found FIXME
				continue;

			// compute "affinity" based on distance and residual
			double affinity = 0;
			double residual = pick->time - (origin->time + ttime.time);
			residual = residual/x;
			residual /= 10; // normalize

			// TODO: REVIEW:
			// test if exp(-residual**2) is better
			affinity = Autoloc::avgfn(residual);
			if (affinity < minimumAffinity)
				continue;

			Association asso(
				origin.get(), pick,
				phaseRange.code, residual, affinity);
			asso.distance = delta;
			asso.azimuth = az;
			associations.push_back(asso);

			// Currently not more than one association per origin
			// TODO: REVIEW. This can be made more efficient!
			break;
		}

		delete ttlist;
	}

	return (associations.size() > 0);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool
Associator::feed(const Autoloc::DataModel::Pick* pick)
{
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<





// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool
Associator::mightBeAssociated(
	const Autoloc::DataModel::Pick* pick,
	const Autoloc::DataModel::Origin *origin) const
{
	// VERY crude first check for P/PKP waves
	double dt = pick->time - origin->time;
	return -10 < dt && dt < 1300;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<





// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Association*
Associator::associate(
	      Autoloc::DataModel::Origin *origin,
	const Autoloc::DataModel::Pick *pick,
	const std::string &phase)
{
	return NULL;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Associator::__OBSOLETE__findPhaseRange(
	const std::string &code, PhaseRange &_pr) const
{
	// Note: The phase code is not necessarily a generic name but more
	// typically a name from a collection of travel time tables like
	// libtau. In other words Pg, Pn, but also P, Pdiff, PKPdf, PKPab,
	// PKPbc etc. and depth phases pP but also pPdiff.

	// exact match
	for (const PhaseRange &pr: _phaseRanges) {
		if (pr.code == code) {
			_pr = pr;
			return true;
		}
	}

	using namespace std;

	// no exact match -> try to find equivalent
	for (const PhaseRange &pr: _phaseRanges) {
		vector<string> basenames = {"P", "S"};

		for (const string &base: basenames) {
			if ((pr.code == base     && code == base+"n") ||
			    (pr.code == base+"n" && code == base)) {
				_pr = pr;
				return true;
			}
			if ((pr.code == base        && code == base+"diff") ||
			    (pr.code == base+"diff" && code == base)) {
				_pr = pr;
				return true;
			}
		}
	}

	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
PhaseRange::PhaseRange(
	const std::string &code,
	double dmin, double dmax, double zmin, double zmax)
	: code(code), dmin(dmin), dmax(dmax), zmin(zmin), zmax(zmax)
{
	// nothing else
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool
Associator::inRange(
	const std::string &code,
	double delta, double depth) const
{
	if (Autoloc::isP(code))
		return 0 <= delta && delta <= 115;

	if (code == "PcP" || code == "ScP")
		return 25 <= delta && delta <= 55;

	if (code == "PP")
		return 60 <= delta && delta <= 160;

	if (code == "PKPab")
		return 140 <= delta && delta <= 180;
	if (code == "PKPbc")
		return 140 <= delta && delta <= 160;
	if (code == "PKPdf")
		return 90 <= delta && delta <= 180;
	if (code == "PKP")
		return 90 <= delta && delta <= 180;

	// For the following phases there are no tables in LocSAT!
	// TODO: Review all of these!
//	if (code == "SKP" || code == "SKPdf")
//		return 120 <= delta && delta <= 150;
	if (code == "PKKP")
		return 80 <= delta && delta <= 130;
//	if (code == "SKKP")
//		return 110 <= delta && delta <= 152;
	if (code == "PKiKP")
		return 30 <= delta && delta <= 120;

	// Phase not found or not in range
	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool
PhaseRange::contains(double delta, double depth) const
{
	if (delta < dmin || delta > dmax)
		return false;

	if (depth < zmin || depth > zmax)
		return false;

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


}  // namespace Seiscomp
