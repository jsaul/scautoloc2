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
#include <seiscomp/autoloc/nucleator.h>
#include <seiscomp/logging/log.h>
#include <seiscomp/core/strings.h>
#include <seiscomp/core/exceptions.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <list>
#include <math.h>

#include <seiscomp/autoloc/util.h>
#include <seiscomp/autoloc/locator.h>
#include <seiscomp/autoloc/sc3adapters.h>


namespace Seiscomp {

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
static std::string station_key (const Autoloc::DataModel::Station *station)
{
	return station->net + "." + station->code;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<





typedef std::set<Autoloc::DataModel::PickCPtr> PickSet;

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Nucleator::setStation(const Autoloc::DataModel::Station *station)
{
	std::string key = station->net + "." + station->code;
	if (_stations.find(key) != _stations.end())
		return; // nothing to insert
	_stations.insert(Autoloc::DataModel::StationMap::value_type(key, station));
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
GridSearchConfig::GridSearchConfig() {
	nmin = 5;
	dmax = 180;
	amin = 5*nmin;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
GridSearch::GridSearch() {
	scconfig = NULL;
//	_stations = 0;
	_abort = false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void GridSearch::setConfig(const Seiscomp::Config::Config *conf) {
	scconfig = conf;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void GridSearch::setStation(const Autoloc::DataModel::Station *station)
{
	Nucleator::setStation(station);
	_relocator.setStation(station);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool GridSearch::init()
{
	_relocator.setConfig(scconfig);

	if ( ! _relocator.init())
		return false;
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool GridSearch::setGridFilename(const std::string &filename)
{
	if (filename.empty())
		throw Seiscomp::Core::ValueException("empty grid file name");
	_gridFilename = filename;
	return _readGrid(filename);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
const std::string& GridSearch::gridFilename() const
{
	return _gridFilename;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void GridSearch::setLocatorProfile(const std::string &profile) {
	_relocator.setProfile(profile);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
int GridSearch::cleanup(const Autoloc::DataModel::Time& minTime)
{
	int count = 0;
	for (GridPointPtr gridpoint : _grid)
		count += gridpoint->cleanup(minTime);

	return count;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




static int _projectedPickCount=0;

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
ProjectedPick::ProjectedPick(const Autoloc::DataModel::Time &t)
	: _projectedTime(t)
{
	_projectedPickCount++;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
ProjectedPick::ProjectedPick(Autoloc::DataModel::PickCPtr p, StationWrapperCPtr w)
	: _projectedTime(p->time - w->ttime), p(p), wrapper(w)
{
	_projectedPickCount++;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
ProjectedPick::ProjectedPick(const ProjectedPick &other)
	: _projectedTime(other._projectedTime), p(other.p), wrapper(other.wrapper)
{
	_projectedPickCount++;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
ProjectedPick::~ProjectedPick()
{
	_projectedPickCount--;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
int ProjectedPick::count()
{
	return _projectedPickCount;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
GridPoint::GridPoint(double latitude, double longitude, double depth)
	: Autoloc::DataModel::Hypocenter(latitude,longitude,depth), _radius(4), _dt(50), maxStaDist(180), _nmin(6), _nminPrelim(4), _origin(new Autoloc::DataModel::Origin(latitude,longitude,depth,0))
{
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
GridPoint::GridPoint(const Autoloc::DataModel::Origin &origin)
	: Autoloc::DataModel::Hypocenter(origin.lat,origin.lon,origin.dep), _radius(4), _dt(50), maxStaDist(180), _nmin(6), _nminPrelim(4), _origin(new Autoloc::DataModel::Origin(origin))
{
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<





// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
const Autoloc::DataModel::Origin*
GridPoint::feed(const Autoloc::DataModel::Pick* pick)
{
	// find the station corresponding to the pick
	const std::string key = station_key(pick->station());

	std::map<std::string, StationWrapperCPtr>::const_iterator
		xit = _wrappers.find(key);
	if (xit==_wrappers.end())
		// this grid cell may be out of range for that station
		return NULL;
	StationWrapperCPtr wrapper = (*xit).second;
	if ( ! wrapper->station ) {
		// TODO test in Nucleator::feed() and use logging
		// TODO at this point probably an exception should be thrown
		SEISCOMP_ERROR("Nucleator: station '%s' not found", key.c_str());
		return NULL;
		
	}

	// At this point we hold a "wrapper" which wraps a station and adds a
	// fews grid-point specific attributes such as the distance from this
	// gridpoint to the station etc.

	// If the station distance exceeds the maximum station distance
	// configured for the grid point...
	if ( wrapper->distance > maxStaDist ) return NULL;

	// If the station distance exceeds the maximum nucleation distance
	// configured for the station...
	if ( wrapper->distance > wrapper->station->maxNucDist )
		return NULL;

	// back-project pick to hypothetical origin time
	ProjectedPick pp(pick, wrapper);

	// store newly inserted pick
	/* std::multiset<ProjectedPick>::iterator latest = */ _picks.insert(pp);

	// roughly test if there is a cluster around the new pick
	std::multiset<ProjectedPick>::iterator it,
		lower  = _picks.lower_bound(pp.projectedTime() - _dt),
		upper  = _picks.upper_bound(pp.projectedTime() + _dt);
	std::vector<ProjectedPick> pps;
	for (it=lower; it!=upper; ++it)
		pps.push_back(*it);

	int npick=pps.size();

	// if the number of picks around the new pick is too low...
	if (npick < _nmin)
		return NULL;

	// now take a closer look at how tightly clustered the picks are
	double dt0 = 4; // XXX
	std::vector<int> _cnt(npick);
	std::vector<int> _flg(npick);
	for (int i=0; i<npick; i++) {
		_cnt[i] = _flg[i] = 0;
	}
	for (int i=0; i<npick; i++) {
		
		ProjectedPick &ppi = pps[i];
		double t_i   = ppi.projectedTime();
		double azi_i = ppi.wrapper->azimuth;
		double slo_i = ppi.wrapper->hslow;

		for (int k=i; k<npick; k++) {
			
			ProjectedPick &ppk = pps[k];
			double t_k   = ppk.projectedTime();
			double azi_k = ppk.wrapper->azimuth;
			double slo_k = ppk.wrapper->hslow;

			double azi_diff = fabs(fmod(((azi_k-azi_i)+180.), 360.)-180.);
			double dtmax = _radius*(slo_i+slo_k) * azi_diff/90. + dt0;

			if (fabs(t_i-t_k) < dtmax) {
				_cnt[i]++;
				_cnt[k]++;

				if(ppi.p == pp.p || ppk.p == pp.p)
					_flg[k] = _flg[i] = 1;
			}
		}
	}
	
	int sum=0;
	for (int i=0; i<npick; i++)
		sum += _flg[i];
	if (sum < _nmin)
		return NULL;

	std::vector<ProjectedPick> group;
	int cntmax = 0;
	Autoloc::DataModel::Time otime;
	for (int i=0; i<npick; i++) {
		if ( ! _flg[i])
			continue;
		group.push_back(pps[i]);
		if (_cnt[i] > cntmax) {
			cntmax = _cnt[i];
			otime = pps[i].projectedTime();
		}
	}


// vvvvvvvvvvvvv Iteration

	std::vector<double> ptime(npick);
	for (int i=0; i<npick; i++) {
		ptime[i] = pps[i].projectedTime();
	}

//	Autoloc::DataModel::Origin* origin = new Autoloc::DataModel::Origin(lat, lon, dep, otime);
	_origin->arrivals.clear();
	// add Picks/Arrivals to that newly created Origin
	std::set<std::string> stations;
	for (unsigned int i=0; i<group.size(); i++) {
		const ProjectedPick &pp = group[i];

		Autoloc::DataModel::PickCPtr pick = pp.p;
		const std::string key = station_key(pick->station());
		// avoid duplicate stations XXX ugly without amplitudes
		if( stations.count(key))
			continue;
		stations.insert(key);

		StationWrapperCPtr sw( _wrappers[key]);

		Autoloc::DataModel::Arrival arr(pick.get());
		arr.residual = pp.projectedTime() - otime;
		arr.distance = sw->distance;
		arr.azimuth  = sw->azimuth;
		arr.excluded = Autoloc::DataModel::Arrival::NotExcluded;
		arr.phase = (pick->time - otime < 960.) ? "P" : "PKP";
//		arr.weight   = 1;
		_origin->arrivals.push_back(arr);
	}

	if (_origin->arrivals.size() < (size_t)_nmin)
		return NULL;

	return _origin.get();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
int GridPoint::cleanup(const Autoloc::DataModel::Time& minTime)
{
	int count = 0;

	// this is for counting only
	std::multiset<ProjectedPick>::iterator it, upper=_picks.upper_bound(minTime);
	for (it=_picks.begin(); it!=upper; ++it)
		count++;

	_picks.erase(_picks.begin(), upper);

	return count;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<





// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool GridPoint::setupStation(const Autoloc::DataModel::Station *station)
{
	double delta=0, az=0, baz=0;
	Autoloc::delazi(this, station, delta, az, baz);

	// Don't setup the grid point for a station if it is out of
	// range for that station - this reduces the memory used by
	// the grid
	if ( delta > station->maxNucDist )
		return false;

	TravelTime tt;
	if ( ! Autoloc::travelTime(lat, lon, dep, station->lat, station->lon, 0, "P1", tt))
		return false;

	StationWrapperCPtr sw = new StationWrapper(station, tt.phase, delta, az, tt.time, tt.dtdd);
	std::string key = station_key (sw->station);
	_wrappers[key] = sw;

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
static PickSet originPickSet(const Autoloc::DataModel::Origin *origin)
{
	PickSet picks;

	int arrivalCount = origin->arrivals.size();
	for(int i=0; i<arrivalCount; i++) {
		Autoloc::DataModel::Arrival &arr = ((Autoloc::DataModel::Origin*)origin)->arrivals[i];
		if (arr.excluded) continue;
		picks.insert(arr.pick);
	}

	return picks;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
static Autoloc::DataModel::Origin* bestOrigin(Autoloc::DataModel::OriginVector &origins)
{
	double maxScore = 0;
	Autoloc::DataModel::Origin* best = 0;

	for (Autoloc::DataModel::OriginPtr origin : origins) {
		double score = Autoloc::originScore(origin.get());
		if (score > maxScore) {
			maxScore = score;
			best = origin.get();
		}
	}

	return best;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool GridSearch::feed(const Autoloc::DataModel::Pick *pick)
{
	using namespace Seiscomp::Autoloc::DataModel;

	_newOrigins.clear();

	if (_stations.size() == 0) {
		SEISCOMP_ERROR("\nGridSearch::feed() NO STATIONS SET\n");
		exit(1);
	}

	std::string key = pick->net() + "." + pick->sta() + "." + pick->loc();

	// link pick to station through pointer

/*
NOT NEEDED - a pick must have a station associated to it by now
	if (pick->station() == 0) {
		StationMap::const_iterator it = _stations.find(key);
		if (it == _stations.end()) {
			SEISCOMP_ERROR_S("\nGridSearch::feed() NO STATION " + key + "\n");
			return false;
		}
		SEISCOMP_ERROR("GridSearch::feed()  THIS SHOULD NEVER HAPPEN");
		pick->setStation((*it).second.get());
	}
*/

	// If not done already, set up the grid for this station now.
	bool stationSetupNeeded = false;
	if (_configuredStations.find(key) == _configuredStations.end()) {
		_configuredStations.insert(key);
		stationSetupNeeded = true;
		SEISCOMP_DEBUG_S("GridSearch: setting up station " + key);
	}

	std::map<PickSet, OriginPtr> pickSetOriginMap;

	// Main loop
	//
	// Feed the new pick into the individual grid points
	// and save all "candidate" origins in originVector

	double maxScore = 0;
	for (GridPointPtr gp : _grid) {
		if (stationSetupNeeded)
			gp->setupStation(pick->station());

		const Origin *result = gp->feed(pick);
		if ( ! result)
			continue;

		// look at the origin, check whether
		//  * it fulfils certain minimum criteria
		//  * we have already seen a similar but better origin

		// test minimum number of picks
		// TODO: make this limit configurable
		if (result->arrivals.size() < 6)
			continue;
		// is the new pick part of the returned origin?
		if (result->findArrival(pick) == -1)
			// this is actually an unexpected condition!
			continue;

		const PickSet pickSet = originPickSet(result);
		// test if we already have an origin with this particular pick set
		if (pickSetOriginMap.find(pickSet) != pickSetOriginMap.end()) {
			double score1 = Autoloc::originScore(pickSetOriginMap[pickSet].get());
			double score2 = Autoloc::originScore(result);
			if (score2<=score1)
				continue;
		}

		double score = Autoloc::originScore(result);
		if (score < 0.6*maxScore)
			continue;

		if (score > maxScore)
			maxScore = score;

/*
		_relocator.useFixedDepth(true);
		OriginPtr relo = _relocator.relocate(origin);
		if ( ! relo)
			continue;

		double delta, az, baz;
		delazi(origin->lat, origin->lon, gp->lat, gp->lon, delta, az, baz);
		if (delta > gp->_radius) // XXX private
			continue;
*/

		OriginPtr newOrigin = new Origin(*result);

		pickSetOriginMap[pickSet] = newOrigin;
	}

	OriginVector tempOrigins;
	for (std::map<PickSet, OriginPtr>::iterator
	     it = pickSetOriginMap.begin(); it != pickSetOriginMap.end(); ++it) {

		Origin *origin = (*it).second.get();
		if (Autoloc::originScore(origin) < 0.6*maxScore)
			continue;

// XXX XXX XXX XXX XXX
// Hier nur jene Origins aus Gridsearch zulassen, die nicht mehrheitlich aus assoziierten Picks bestehen.
// XXX XXX XXX XXX XXX

		_relocator.useFixedDepth(true); // XXX vorher true
		OriginPtr relo = _relocator.relocate(origin);
		if ( ! relo)
			continue;

		// see if the new pick is within the maximum allowed nucleation distance
		int index = relo->findArrival(pick);
		if (index==-1) {
			SEISCOMP_ERROR("pick unexpectedly not found in GridSearch::feed()");
			continue;
		}
		if (relo->arrivals[index].distance > pick->station()->maxNucDist)
			continue;

		tempOrigins.push_back(relo);
	}


	// Now for all "candidate" origins in tempOrigins try to find the
	// "best" one. This is a bit problematic as we don't go back and retry
	// using the second-best but give up here. Certainly scope for
	// improvement.

	OriginPtr best = bestOrigin(tempOrigins);
	if (best) {
		_relocator.useFixedDepth(false);
		OriginPtr relo = _relocator.relocate(best.get());
		if (relo)
			_newOrigins.push_back(relo);
	}

	return _newOrigins.size() > 0;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool GridSearch::_readGrid(const std::string &gridfile)
{
	std::ifstream ifile(gridfile.c_str());

	if ( ifile.good() )
		SEISCOMP_DEBUG_S("Reading gridfile " + gridfile);
	else {
		SEISCOMP_ERROR_S("Failed to read gridfile " + gridfile);
		return false;
	}

	_grid.clear();
	double lat, lon, dep, rad, dmax; int nmin;
	while ( ! ifile.eof() ) {
		std::string line;
		std::getline(ifile, line);

		Seiscomp::Core::trim(line);

		// Skip empty lines
		if ( line.empty() ) continue;

		// Skip comments
		if ( line[0] == '#' ) continue;

		std::istringstream iss(line, std::istringstream::in);

		if (iss >> lat >> lon >> dep >> rad >> dmax >> nmin) {
			GridPoint *gp = new GridPoint(lat, lon, dep);
			gp->_nmin = nmin;
			gp->_radius = rad;
			gp->maxStaDist = dmax;
			_grid.push_back(gp);
		}
	}
	SEISCOMP_DEBUG("read %d grid lines",int(_grid.size()));
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void GridSearch::setup()
{
//	_relocator.setStations(_stations);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


}  // namespace Seiscomp






















































/*
		TravelTimeList *ttlist = ttt.compute(
			_latitude, _longitude, _depth, slat, slon, salt);

		TravelTime tt;
		for (TravelTimeList::iterator it = ttlist->begin();
		     it != ttlist->end(); ++it) {
			tt = *it;
			if (delta < 114)
			// for  distances < 114, allways take 1st arrival
				break;
			if (tt.phase == "Pdiff")
			// for  distances >= 114, skip Pdiff, take next
				continue;
			break;
		}
		delete ttlist;
*/
