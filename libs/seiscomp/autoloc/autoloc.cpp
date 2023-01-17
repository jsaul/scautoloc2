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


#define SEISCOMP_COMPONENT Autoloc2
// These defines are only used in testing during development:
//#define EXTRA_DEBUGGING

#define LOG_RELOCATOR_CALL \
	SEISCOMP_DEBUG("RELOCATE autoloc.cpp line %d", __LINE__)

#define THIS_SHOULD_NEVER_HAPPEN \
	SEISCOMP_ERROR("THIS SHOULD NEVER HAPPEN @ autoloc.cpp line %d", __LINE__)

#define RELOCATION_FAILED_WARNING \
	SEISCOMP_WARNING("RELOCATION FAILED @ autoloc.cpp line %d", __LINE__);

#define NEVER_RUN_THIS_LOOP \
	while(false)

#include <seiscomp/autoloc/autoloc.h>
#include <seiscomp/autoloc/util.h>
#include <seiscomp/autoloc/sc3adapters.h>
#include <seiscomp/autoloc/nucleator.h>

#include <seiscomp/logging/log.h>
#include <seiscomp/seismology/ttt.h>
#include <seiscomp/datamodel/utils.h>
#include <seiscomp/datamodel/network.h>
#include <seiscomp/datamodel/station.h>

#include <algorithm>

namespace Seiscomp {

namespace Autoloc {

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Autoloc3::Autoloc3()
{
	_now = _nextCleanup = 0;
	_associator.setOrigins(&_origins);
	_associator.setPickPool(&pickPool);
	_relocator.setMinimumDepth(_config.minimumDepth);
	scconfig = nullptr;
	processingEnabled = true;

	// TODO/FIXME: Important config option
	associateDisabledStationsToQualifiedOrigin = true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::init()
{
	if ( ! scinventory) {
		SEISCOMP_ERROR("Missing SeisComP Inventory");
		return false;
	}

	if ( ! scconfig) {
		SEISCOMP_WARNING("Missing SeisComP Config");
		// return false;
	}

	if ( ! _config.stationConfig.empty()) {
		SEISCOMP_DEBUG_S(
			"Reading station config from file '" +
			_config.stationConfig +"'");

		_stationConfig.setFilename(_config.stationConfig);
		if ( ! _stationConfig.read())
			return false;
	}

	_nucleator.setConfig(scconfig);
	if ( ! _nucleator.setGridFilename(_config.gridConfigFile))
		return false;
	if ( ! _nucleator.init())
		return false;

	_relocator.setConfig(scconfig);
	if ( ! _relocator.init())
		return false;

	_relocator.setMinimumDepth(_config.minimumDepth);

	setLocatorProfile(_config.locatorProfile);

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::dumpState() const
{
	for (const Autoloc::DataModel::OriginPtr &origin : _origins)
		SEISCOMP_INFO_S(printOneliner(origin.get()));
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_addStationInfo(const Autoloc::DataModel::Pick *pick)
{
	using namespace Autoloc::DataModel;

	if (pick->station())
		return true;

	const std::string key =
		pick->net() + "." + pick->sta() + "." + pick->loc();
	StationMap::const_iterator it = _stations.find(key);
	if (it == _stations.end()) {

		// remember missing stations we already complained about
		if (_missingStations.find(key) == _missingStations.end()) {
			SEISCOMP_ERROR_S("MISSING STATION " + key);
			_missingStations.insert(key);
		}
		return false;
	}

	pick->setStation(it->second.get());

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::pickInPool(const std::string &id) const
{
	return pickFromPool(id) != nullptr;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
const Autoloc::DataModel::Pick*
Autoloc3::pickFromPool(const std::string &id) const
{
	using namespace Autoloc::DataModel;

	PickPool::const_iterator it = pickPool.find(id);
	if (it == pickPool.end())
		return nullptr;

	const Pick* pick = it->second.get();

	return pick;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Seiscomp::Core::Time
Autoloc3::now()
{
	return sctime(_now);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::storeInPool(const Autoloc::DataModel::Pick *pick)
{
	if ( ! pickFromPool(pick->id())) {
		pickPool[ pick->id() ] = pick;
SEISCOMP_DEBUG_S("Autoloc3::storeInPool "+pick->id());
		return true;
	}

	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::feed(const Seiscomp::DataModel::Pick *scpick)
{
	if ( ! scpick ) {
		SEISCOMP_ERROR("Got NULL Pick");
		return false;
	}

	const std::string &pickID = scpick->publicID();

	if (_config.playback) {
		try {
			const Core::Time &creationTime =
				scpick->creationInfo().creationTime();
			sync(creationTime);
		}
		catch(...) {
			SEISCOMP_WARNING_S(
				"Pick "+pickID+" without creation time!");
		}
	}

	// Set Pick EvaluationMode to AUTOMATIC if missing.
	try {
		scpick->evaluationMode();
	}
	catch ( ... ) {
		using namespace Seiscomp::DataModel;
		SEISCOMP_WARNING_S("Pick has no evaluation mode: " + pickID);
		SEISCOMP_WARNING  ("Setting evaluation mode to AUTOMATIC");
		const_cast<Pick*>(scpick)->setEvaluationMode(
			EvaluationMode(AUTOMATIC));
	}

	Autoloc::DataModel::Pick* pick = new Autoloc::DataModel::Pick(scpick);
	if ( ! pick )
		return false;

	if ( ! setupStation(scpick))
		return false;

	if ( ! _addStationInfo(pick))
		return false;

	// Pick priority based on author
	const std::string &author = objectAuthor(scpick);
	pick->priority = _authorPriority(author);
	SEISCOMP_INFO(
		"pick '%s' from author '%s' has priority %d",
		pickID.c_str(), author.c_str(), pick->priority);
	return feed(pick);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::feed(const Seiscomp::DataModel::Amplitude *scampl)
{
	if ( ! scampl ) {
		SEISCOMP_ERROR("Got NULL Amplitude");
		return false;
	}

	const std::string &atype  = scampl->type();
	const std::string &pickID = scampl->pickID();

	if (atype != _config.amplTypeAbs &&
	    atype != _config.amplTypeSNR)
		return false;

	Autoloc::DataModel::Pick *pick =
		const_cast<Autoloc::DataModel::Pick*>(pickFromPool(pickID));

	// The pick must be received before the amplitudes!
	if ( ! pick ) {
		SEISCOMP_WARNING_S("Pick " +pickID+ " not found in pick pool");
		return false;
	}

	try {
		if ( atype == _config.amplTypeSNR )
			pick->setAmplitudeSNR(scampl);
		else
		if ( atype == _config.amplTypeAbs )
			pick->setAmplitudeAbs(scampl);
	}
	catch ( ... ) {
		return false;
	}

	return feed(pick);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::isTrustedOrigin(const Seiscomp::DataModel::Origin *scorigin) const
{
	if ( ! scorigin ) {
		THIS_SHOULD_NEVER_HAPPEN;
		return false;
	}

	bool importedOrigin = objectAgencyID(scorigin) != _config.agencyID;

	if (importedOrigin) {
		if ( ! _config.useImportedOrigins ) {
			SEISCOMP_INFO_S(
				"Ignored origin from " +
				objectAgencyID(scorigin) + " because "
				" autoloc.useImportedOrigins = false");
			return false;
		}

/*
		// Currently this filtering takes place in the application class
		// TODO: Review!
		if ( isAgencyIDBlocked(objectAgencyID(scorigin)) ) {
			SEISCOMP_INFO_S("Ignored origin from " +
				objectAgencyID(scorigin) + " because "
				"this agency ID is blocked");
			return false;
		}
*/
	}
	else {
		if ( manual(scorigin) ) {
			if ( ! _config.useManualOrigins ) {
				SEISCOMP_INFO_S(
					"Ignored manual origin from " +
					objectAgencyID(scorigin) + " because "
					"autoloc.useManualOrigins = false");
				return false;
			}
		}
		else {
			// own origin which is not manual -> ignore
			SEISCOMP_INFO_S(
				"Ignored origin from " +
				objectAgencyID(scorigin) + " because "
				" not a manual origin");
			return false;
		}
	}

	// At this point we know that the origin is either
	//  * imported from a trusted external source or
	//  * an internal, manual origin

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::feed(Seiscomp::DataModel::Origin *scorigin)
{
	// The origin MUST be either
	//  * imported from a trusted external source or
	//  * an internal, manual origin
	// This is not checked and must be checked beforehand!

	if ( ! isTrustedOrigin(scorigin) )
		return false;

	// FIXME: Currently, importFromSC may require database access if
	// certain picks/amplitudes are missing.
	// TODO: This should be avoided and all needed objects should be
	// accessible without a database.
	Autoloc::DataModel::Origin *origin = importFromSC(scorigin);
	if ( ! origin ) {
		SEISCOMP_ERROR_S(
			"Failed to import origin " + scorigin->publicID());
		return false;
	}

	SEISCOMP_INFO_S(
		"Using origin from agency " + objectAgencyID(scorigin));

	return feed(origin);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::setProcessingEnabled(bool v)
{
	processingEnabled = v;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::isProcessingEnabled() const
{
	return processingEnabled;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::feed(const Autoloc::DataModel::Pick *pick)
{
	_newOrigins.clear();

	using namespace Autoloc::DataModel;
/*
	TODO!
	if (_expired(pick)) {
		SEISCOMP_INFO_S("ignoring expired pick " + pick->id());
		return false;
	}
*/

	// Store pick in pick pool irrespective of whether it shall be
	// processed or not.
	//
	// For instance, an automatic pick from a disabled station is
	// not processed. But an analyst might manually associate it,
	// so we do need to add it to the pool.

	bool isnew = storeInPool(pick);

	if ( ! processingEnabled) {
		SEISCOMP_INFO(
			"process pick %-35s %c   "
			"processing currently disabled",
			pick->id().c_str(), statusFlag(pick));

		// Note that pick has been stored in pick pool!	
		return false;
	}

	// An associated manual pick is always processed, even from a
	// disabled station, because this is an analyst's decision,
	// which we honor.
	// TODO: review
	if (automatic(pick) && ! pick->station()->enabled) {
		SEISCOMP_INFO(
			"pick %-35s not processed (station disabled)",
			pick->id().c_str());

		// Note that pick has been stored in pick pool!	
		return false;
	}

	if (pick->priority <= 0) {
		SEISCOMP_INFO(
			"pick '%s' not automatically processed (priority 0)",
			pick->id().c_str());

		// Note that pick has been stored in pick pool!	
		return false;
	}

	if (_requiresAmplitude(pick)) {
		if ( ! hasAmplitude(pick)) {
			if (isnew)
				// Delay pick processing until
				// all amplitudes are present
				SEISCOMP_DEBUG(
					"process pick %-35s %c   waiting for amplitude",
					pick->id().c_str(), statusFlag(pick));
			return false;
		}
	}
	else {
		// FIXME: TEMP casts...
		double defaultAmplitudeSNR = 10;
		double defaultAmplitudeAbs = 1;
		if (pick->snr <= 0)
			const_cast<Pick*>(pick)->snr = defaultAmplitudeSNR;
		if (pick->amp <= 0)
			const_cast<Pick*>(pick)->amp = defaultAmplitudeAbs;
		if (pick->per <= 0)
			const_cast<Pick*>(pick)->per = 1.;
	}

	// Find picks for the same station at nearly the same time.
	// This can happen if two pickers accidentally run at the same time or
	// if duplicate records in the waveform data stream result in
	// duplicate picks at almost exactly the same time. We don't want such
	// duplicates to spoil our solutions so we detect this situation here
	// and mark duplicate picks as blacklisted
	for (const auto &item: pickPool) {

		const Pick *existingPick = item.second.get();

		if (existingPick->station() != pick->station())
			continue;
		if (existingPick->id() == pick->id())
			continue;
		double dt = std::abs(existingPick->time - pick->time);
		if (dt < 1) {
			SEISCOMP_DEBUG(
				"colliding picks %s and %s",
				pick->id().c_str(), existingPick->id().c_str());
// NO BLACKLISTING AT THIS POINT
//			SEISCOMP_DEBUG(
//				"blacklisting pick %s", pick->id().c_str());
//			pick->blacklisted = true;
		}
	}

	const Pick *p = pickFromPool(pick->id());
	if (p->blacklisted)
		return false;


	// A pick is tagged as XXL pick if it exceeds BOTH the configured XXL
	// minimum amplitude and XXL minimum SNR threshold.
	if ( _config.xxlEnabled &&
	     p->amp >= _config.xxlMinAmplitude &&
	     p->snr >  _config.xxlMinSNR ) {
		SEISCOMP_DEBUG_S("XXL pick " + p->scpick->publicID());
		const_cast<Pick*>(p)->xxl = true;
	}

	// arbitrary choice
	// TODO: review, perhaps make configurable
	double normalizationAmplitude = 2000.;
	if ( _config.xxlEnabled )
		normalizationAmplitude = _config.xxlMinAmplitude;
	const_cast<Pick*>(p)->normamp = p->amp/normalizationAmplitude;


	bool result = _process(p);
	if ( ! result)
		return false;

	report();
	cleanup();

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Autoloc::DataModel::OriginID
Autoloc3::_findMatchingOrigin(const Autoloc::DataModel::Origin *origin) const
{
	using namespace Autoloc::DataModel;

	// find commonalities with existing origins
	// * identical picks
	// * similar picks (same stream but slightly different times)
	// replace similar picks by the ones found in the new origin,
	// incl. weight
	Origin *found = 0;
	size_t bestmatch = 0;

	// iterate over existing origins
	for (OriginPtr existing : _origins) {

		// It makes no sense to compare origins too different in time.
		// This maximum time difference is for teleseismic "worst case"
		// where we might need to associate origins wrongly located e.g.
		// by using PKP as P where time differences of up to 20 minutes
		// are possible.
		//
		// This time difference may be made configurable but this is
		// not crucial.
		if (std::abs(origin->time - existing->time) > 20*60)
			continue;

		size_t identical=0, similar=0;

		// look for manual picks associated to this origin
		for (auto arr: existing->arrivals) {
			const Pick *pick = arr.pick.get();

			if ( ! pick->station()) {
				SEISCOMP_WARNING(
					"Pick %s without station",
					pick->id().c_str());
				continue;
			}

			// try to find a matching pick in our newly fed origin
			for (auto arr2: origin->arrivals) {
				const Pick *pick2 = arr2.pick.get();

				// TODO: adopt arrival weight etc.

				// identical picks?
				if (pick2 == pick) {
					identical++;
					break;
				}

				// picks for same station
				// and less than +/- 20 s in time
				double max_dt = 20;  // TODO: review
				if (pick2->station() == pick->station()) {
					double dt = pick2->time - pick->time;
					if (-max_dt <= dt && dt <= max_dt) {
						similar++;
						break;
					}
				}
			}
		}

		if (identical+similar > 0) {
			if (identical+similar > bestmatch) {
				bestmatch = identical+similar;
				found = existing.get();
			}
		}
	}

	return found ? found->id : -1;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::feed(Autoloc::DataModel::Origin *origin)
{
	if ( imported(origin) )
		return _processImportedOrigin(origin);

	if ( manual(origin) )
		return _processManualOrigin(origin);

	if ( manual(origin) || imported(origin) )
		return _processQualifiedOrigin(origin);

	THIS_SHOULD_NEVER_HAPPEN;
	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
int Autoloc3::_authorPriority(const std::string &author) const
{
	if (_config.pickAuthors.empty())
		// same priority for every author
		return 1;

	auto it = std::find(
		_config.pickAuthors.begin(),
		_config.pickAuthors.end(),
		author);

	return _config.pickAuthors.end() - it;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
double Autoloc3::_score(const Autoloc::DataModel::Origin *origin) const
{
	// Compute the score of the origin as if there were no other origins
	double score = Autoloc::originScore(origin);

	// TODO: See how many of the picks may be secondary phases of a
	// previous origin
	return score;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_log(const Autoloc::DataModel::Pick *pick)
{
	if (_pickLogFilePrefix != "")
		setPickLogFileName(_pickLogFilePrefix+"."+sctime(_now).toString("%F"));

	if ( ! _pickLogFile.good())
		return false;

	char line[200];
	std::string loc = pick->loc() == "" ? "__" : pick->loc();
	sprintf(line, "%s %-2s %-6s %-3s %-2s %6.1f %10.3f %4.1f %c %s",
		time2str(pick->time).c_str(),
		pick->net().c_str(), pick->sta().c_str(), pick->cha().c_str(), loc.c_str(),
		pick->snr, pick->amp, pick->per, statusFlag(pick),
		pick->id().c_str());
	_pickLogFile << line << std::endl;

	SEISCOMP_INFO("%s", line);

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_tooLowSNR(const Autoloc::DataModel::Pick *pick) const
{
	if ( ! automatic(pick))
		return false;

	if (pick->snr < _config.minPickSNR)
		return true;

	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_tooManyRecentPicks(const Autoloc::DataModel::Pick *newPick) const
{
	using namespace Autoloc::DataModel;

	if ( ! automatic(newPick))
		return false;

	double weightedSum = 0, prevThreshold = 0,
	       timeSpan = _config.dynamicPickThresholdInterval;

	if (timeSpan <= 0)
		return false;
/*
	if (newPick->snr <= 0.) {
		SEISCOMP_DEBUG(
			"_tooManyRecentPicks: new pick without snr "
			"amplitude: %s -> ignored  (%g)",
			newPick->id().c_str(), newPick->snr);
		return true;
	}
*/
	for (const auto &item: pickPool) {
		const Pick *previousPick = item.second.get();

		if (previousPick->station() != newPick->station())
			continue;

		if (ignored(previousPick))
			continue;

		if ( ! _config.useManualPicks && manual(previousPick) &&
		     ! _config.useManualOrigins )
			continue;

		double dt = newPick->time - previousPick->time;
		if (dt < 0 || dt > timeSpan)
			continue;

		double snr = previousPick->snr;
		if (snr > 15)  snr = 15;
		if (snr <  3)  snr =  3;
		weightedSum += snr * (1-dt/timeSpan);


		// not well tested:
		double x = snr * (1-dt/_config.xxlDeadTime);
		if (x > prevThreshold)
			prevThreshold = x;
	}

	// These criteria mean that if within the time span there
	// were 10 Picks with SNR X
	weightedSum *= 2*0.07; // TODO: Make 0.07 configurable?
	if (newPick->snr < weightedSum) {
		SEISCOMP_DEBUG("_tooManyRecentPicks: %-35s      %.2f < %.2f",
			      newPick->id().c_str(), newPick->snr, weightedSum);
		return true;
	}

	if (newPick->snr < prevThreshold) {
		SEISCOMP_DEBUG("_tooManyRecentPicks: %-35s   XX %.2f < %.2f",
			       newPick->id().c_str(), newPick->snr,
			       prevThreshold);
		return true;
	}

	// OK, pick passes check
	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Autoloc::DataModel::Origin*
Autoloc3::merge(
	const Autoloc::DataModel::Origin *origin1,
	const Autoloc::DataModel::Origin *origin2)
{
	using namespace Autoloc::DataModel;

	// The second origin is merged into the first. A new instance
	// is returned that has the ID of the first.
	OriginID id = origin1->id;

	// make origin1 the better origin
	if (_score(origin2) > _score(origin1)) {
		const Origin *tmp = origin1;
		origin1 = origin2;
		origin2 = tmp;
	}

	Origin *combined = new Origin(*origin1);
	combined->id = id;

	SEISCOMP_DEBUG_S(" MRG1 " + printOneliner(origin1));
	SEISCOMP_DEBUG_S(" MRG2 " + printOneliner(origin2));

	// This is a brute-force merge! Put everything into one origin.
	for (auto &a2: origin2->arrivals) {
		// Skip pick if an arrival already references it
		bool found = combined->findArrival(a2.pick.get()) != -1;
		if (found)
			continue;

		// Skip pick if origin1 already has a pick from that station
		// for the same phase.
		for (auto &a1: origin1->arrivals) {
			if (a1.pick->station() == a2.pick->station() &&
			    a1.phase == a2.phase) {
				found = true;
				break;
			}
		}
		if (found)
			continue;

		Arrival tmp = a2;
		tmp.excluded = Arrival::TemporarilyExcluded;
		// FIXME: The phase ID may not match.
		combined->add(tmp);
		SEISCOMP_DEBUG(
			" MRG %ld->%ld added %s",
			origin2->id, origin1->id, a2.pick->id().c_str());
	}

	LOG_RELOCATOR_CALL;

	// This was previously missing:
	_relocator.useFixedDepth(false);  // TODO: extensive testing!

	OriginPtr relo = _relocator.relocate(combined);
	if ( ! relo) {
		// Actually we expect the relocation to always succeed,
		// because the temporarily excluded new arrivals should
		// not influence the solution. It does happen, rarely,
		// but is NOT critical.
		SEISCOMP_WARNING("Failed relocation after merge");

		// The returned origin is the better of the two original
		// origins with the merged arrivals now TemporarilyExcluded.
		return nullptr;
	}

	combined->updateFrom(relo.get());

	// now see which of the temporarily excluded new arrivals have
	// acceptable residuals
	for (auto &a : combined->arrivals) {
		if (a.excluded == Arrival::TemporarilyExcluded)
			a.excluded = _residualWithinAllowedRange(a, 1.3, 1.8)
				? Arrival::NotExcluded
				: Arrival::LargeResidual;
	}

	_trimResiduals(combined);

	return combined;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_followsBiggerPick(
	const Autoloc::DataModel::Pick *newPick) const
{
	using namespace Autoloc::DataModel;

	// Check whether this pick is within a short time
	// after an XXL pick from the same station
	for (const auto &item: pickPool) {
		const Pick *pick = item.second.get();

		if (pick == newPick)
			continue;

		if ( ! pick->xxl)
			continue;

		if (pick->station() != newPick->station())
			continue;

		double dt = newPick->time - pick->time;
		if (dt < 0 || dt > _config.xxlDeadTime)
			continue;

		SEISCOMP_INFO_S(
			"process pick IGNORING " + newPick->id() +
			" (following XXL pick" + pick->id() + ")");
		return true;
	}

	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_perhapsPdiff(const Autoloc::DataModel::Pick *pick) const
{
	using namespace Autoloc::DataModel;

	// This is a very crude test that won't harm. if at all, only a few
	// picks with low SNR following a large event are affected.


	// TODO: make this configurable? not very important
	if (pick->snr > 6)
		return false;

	bool result = false;

	for (const OriginPtr &origin : _origins) {
		const Station *station = pick->station();

		if (pick->time - origin->time > 1000)
			continue;

		// TODO: make this configurable? not very important
		if (origin->score < 100)
			continue;

		double delta, az, baz;
		delazi(origin.get(), station, delta, az, baz);

		if (delta < 98 || delta > 120)
			continue;

		Seiscomp::TravelTimeTable ttt;
		Seiscomp::TravelTimeList *ttlist =
			ttt.compute(origin->lat, origin->lon, origin->dep,
				    station->lat, station->lon, 0);
		const Seiscomp::TravelTime *tt;
		if ( (tt = getPhase(ttlist, "Pdiff")) == nullptr ) {
			delete ttlist;
			continue;
		}
		delete ttlist;

		double dt = pick->time - (origin->time + tt->time);
		if (dt > 0 && dt < 150) {
			SEISCOMP_DEBUG(
				"Pick %s in Pdiff coda of origin %ld",
				pick->id().c_str(), origin->id);
			result = true;
		}
	}

	return result;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Autoloc::DataModel::OriginPtr
Autoloc3::_xxlPreliminaryOrigin(const Autoloc::DataModel::Pick *newPick)
{
	using namespace Autoloc::DataModel;

	if ( ! newPick->xxl)
		// nothing else to do for this pick
		return 0;

	OriginPtr newOrigin = 0;

	std::vector<const Pick*> xxlpicks;
	const Pick *earliest = newPick;
	xxlpicks.push_back(newPick);
	for (const auto &item: pickPool) {
		const Pick *pick = item.second.get();

		if ( ! pick->xxl )
			continue;

		if ( ignored(pick) )
			continue;
				
		if ( newPick->station() == pick->station() )
			continue;

		double dt = newPick->time - pick->time;
		double dx = distance(pick->station(), newPick->station());

		if (std::abs(dt) > 10+13.7*_config.xxlMaxStaDist)
			continue;

		if ( dx > _config.xxlMaxStaDist )
			continue;

		if ( ! _config.useManualPicks && manual(pick) &&
		     ! _config.useManualOrigins )
			continue;

		// make sure we don't have two picks of the same station
		bool duplicate_station = false;
		for (const Pick* pick: xxlpicks) {
			if (pick->station() == pick->station()) {
				duplicate_station = true;
				break;
			}
		}
		if ( duplicate_station )
			continue;

		xxlpicks.push_back(pick);

		if ( pick->time < earliest->time )
			earliest = pick;
	}

	SEISCOMP_DEBUG("Number of XXL picks=%ld", xxlpicks.size());
	if (xxlpicks.size() < _config.xxlMinPhaseCount)
		return nullptr;

	double lat = earliest->station()->lat+0.03;
	double lon = earliest->station()->lon+0.03;
	double tim = earliest->time-0.05;
	double dep;

	// loop over several trial depths, which are multiples
	// of the default depth
	std::vector<double> trialDepths;
	for (int i=0; dep <= _config.xxlMaxDepth; i++) {
		dep = _config.defaultDepth*(1+i);
		trialDepths.push_back(dep);

		// in case of "sticky" default depth,
		// we don't need any more trial depths
		// TODO: review criterion
		if (_config.defaultDepthStickiness > 0.9)
			break;
	}
	
	for (double dep: trialDepths) {
		OriginPtr origin = new Origin(lat, lon, dep, tim);

		for (const Pick* pick: xxlpicks) {
			double delta, az, baz;
			Arrival arr(pick);
			delazi(origin.get(), arr.pick->station(),
			       delta, az, baz);
			arr.distance = delta;
			arr.azimuth = az;
			arr.excluded = Arrival::NotExcluded;
			origin->arrivals.push_back(arr);
		}
		_relocator.setFixedDepth(dep);
		_relocator.useFixedDepth(true);
		SEISCOMP_DEBUG(
			"Trying to relocate possible XXL origin; "
			"trial depth %g km", dep);
		SEISCOMP_DEBUG_S(printDetailed(origin.get()));
		LOG_RELOCATOR_CALL;
		OriginPtr relo = _relocator.relocate(origin.get());
		if ( ! relo) {
			RELOCATION_FAILED_WARNING;
			// next fixed depth
			continue;
		}
		SEISCOMP_DEBUG_S("XXL " + printOneliner(relo.get()));

		bool ignore = false;
		for (const Arrival &arr: relo->arrivals) {
			if (arr.distance > _config.xxlMaxStaDist)
				ignore = true;
		}
		if (relo->rms() > _config.maxRMS)
			ignore = true;
		if (ignore)
			continue;

		SEISCOMP_INFO("RELOCATED XXL ALERT");
		origin->updateFrom(relo.get());
		origin->preliminary = true;
		origin->depthType = _config.defaultDepthStickiness > 0.9
			? Origin::DepthDefault
			: Origin::DepthManuallyFixed;
		SEISCOMP_INFO_S(printOneliner(origin.get()));

		// TODO: The _depthIsResolvable part needs review and
		// could probably be cleaned up a bit...
		if (_config.defaultDepthStickiness < 0.9 &&
		    _depthIsResolvable(origin.get()))
		{
			_relocator.useFixedDepth(false);
			LOG_RELOCATOR_CALL;
			relo = _relocator.relocate(origin.get());
			if (relo)
				origin->updateFrom(relo.get());
		}

		newOrigin = origin;
		break;
	}

	if (newOrigin) {
		newOrigin->id = _newOriginID();
		newOrigin->arrivals.sort();
		return newOrigin;
	}

	return nullptr;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Autoloc::DataModel::OriginID Autoloc3::_newOriginID()
{
	using namespace Autoloc::DataModel;

	static OriginID id = 0;
	return ++id;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
const Autoloc::DataModel::Pick *Autoloc3::supersedesAnotherPick(const Autoloc::DataModel::Pick *pick)
{
	using namespace Autoloc::DataModel;

	const Autoloc::DataModel::Pick *supersededPick = nullptr;

	for (auto &item: pickPool) {

		const Pick *existingPick = item.second.get();

		if (existingPick->blacklisted)
			continue;
		if (existingPick->station() != pick->station())
			continue;
		if (existingPick->id() == pick->id())
			continue;

		double dt = pick->time - existingPick->time; // TODO: asymmetric
		double dtmax = 5;  // make configurable
		if (std::abs(dt) < dtmax) {
			// If we already have a pick from that station and if that
			// pick has a higher priority, we forget our new pick right
			// here.
			if (pick->priority <= existingPick->priority) {
				SEISCOMP_DEBUG(
					"higher-priority pick %s found, no further processing",
					existingPick->id().c_str());
				
				pick->blacklisted = true;
			}
			else {
				SEISCOMP_DEBUG_S(
					"pick "+pick->id()+" supersedes "+existingPick->id());
				supersededPick = existingPick;
			}
		}
	}

	return supersededPick; // which may be NULL
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Autoloc::DataModel::OriginPtr
Autoloc3::_trySupersede(const Autoloc::DataModel::Pick *pick)
{
	//
	// See if there is an existing pick to be superseded by the current
	// pick because the current pick has higher priority.
	//

	using namespace Autoloc::DataModel;

	OriginPtr associatedOrigin = nullptr;

	for (auto &item: pickPool) {

		const Pick *existingPick = item.second.get();

		if (existingPick->blacklisted)
			continue;
		if (existingPick->station() != pick->station())
			continue;
		if (existingPick->id() == pick->id())
			continue;

		double dt = pick->time - existingPick->time; // TODO: asymmetric
		double dtmax = 5;  // make configurable
		if (std::abs(dt) < dtmax) {
			// Have we found a matching origin previously?
			// That should not be the case. -> Warning!
			if (associatedOrigin) {
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			// If we already have a pick from that station and if that
			// pick has a higher priority, we forget our new pick right
			// here.
			if (pick->priority <= existingPick->priority) {
				SEISCOMP_DEBUG(
					"higher-priority pick %s found, no further processing",
					existingPick->id().c_str());
				
				pick->blacklisted = true;
			}
			else {
				SEISCOMP_DEBUG_S(
					"pick "+pick->id()+" supersedes "+existingPick->id());
				SEISCOMP_DEBUG_S(
					"blacklisting pick " + existingPick->id());
				existingPick->blacklisted = true;


				if (existingPick->originID()) {
					const Origin *origin = _origins.find(existingPick->originID());
					if (origin == nullptr) {
						THIS_SHOULD_NEVER_HAPPEN;
						SEISCOMP_ERROR("Origin %ld referenced by pick %s not found", existingPick->originID(), existingPick->id().c_str());
						continue;
					}
SEISCOMP_DEBUG_S(" TMP- " + printOneliner(origin));
					associatedOrigin =
						new Origin(*origin);
SEISCOMP_DEBUG_S(" TMP+ " + printOneliner(associatedOrigin.get()));
					int iarr = associatedOrigin->findArrival(existingPick);
					if (iarr==-1) {
						THIS_SHOULD_NEVER_HAPPEN;
						continue;
					}
					Arrival &a = associatedOrigin->arrivals[iarr];

					bool success = _associate(
						associatedOrigin.get(), pick, a.phase);
					if (success) {
						SEISCOMP_WARNING(
							"Associated pick %s to origin %ld",
							pick->id().c_str(), associatedOrigin->id);
						// once again...
						iarr = associatedOrigin->findArrival(existingPick);
						Arrival &a = associatedOrigin->arrivals[iarr];
// FIXME:
// There is a catch with blacklisting, as we may have replaced a pP pick wrongly labeled
// as P by a legitimate P pick but still want to be able to use the previous pick correctly
// as pP. Blacklisting deprives us from that possibility.
						a.excluded = Arrival::BlacklistedPick;
					}
					else {
						SEISCOMP_WARNING(
							"Failed to associate pick %s to origin %ld",
							pick->id().c_str(), associatedOrigin->id);
						associatedOrigin = nullptr;
						continue;
					}
				}
			}
		}
	}

	return associatedOrigin; // which may be nullptr
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Autoloc::DataModel::OriginPtr
Autoloc3::_tryAssociate(const Autoloc::DataModel::Pick *pick)
{
	//
	// Try to associate one pick with an existing
	// origin, assuming that the pick is a P phase.
	//
	using namespace Autoloc::DataModel;

	bool trackedPick = isTrackedPick(pick->id());
trackedPick=true; // TEMP

	// log tracked pick
	if (trackedPick)
		SEISCOMP_DEBUG(
			"Trying to associate pick %s to existing origin",
			pick->id().c_str());


	OriginPtr origin = nullptr;

	AssociationVector associations;
	bool success = _associator.findMatchingOrigins(pick, associations);
	if ( ! success) {
		if (trackedPick)
			SEISCOMP_DEBUG("No matching origin found");
		return nullptr;
	}

	if (trackedPick)
		SEISCOMP_DEBUG(
			"Resulting in %ld associations",
			associations.size());

	// log all associations
	for (auto &a: associations) {
		SEISCOMP_INFO_S(
			"     " + printOneliner(a.origin.get()) +
			"  ph=" + a.phase);
		SEISCOMP_INFO  (
			"     aff=%.2f res=%.2f",
			a.affinity, a.residual);
	}

	//
	// Loop through the associations.
	// Each association refers to a different origin.
	//


	// Only look for possibly associated imported origins
	for (auto &a: associations) {
		if ( ! imported(a.origin.get()))
			continue;

		// A bit redundant. TODO: Let the associator check this.
		if (a.affinity < _config.minPickAffinity)
			continue;

		OriginPtr associatedOrigin = new Origin(*a.origin.get());

		bool success = _associate(
			associatedOrigin.get(), pick, a.phase);
		if ( ! success) {
			SEISCOMP_WARNING(
				"Failed to associate pick %s to origin %ld",
				pick->id().c_str(), associatedOrigin->id);
			continue;
		}

		int index = associatedOrigin->findArrival(pick);
		if (index==-1) {
			THIS_SHOULD_NEVER_HAPPEN;
			return nullptr;
		}
		Arrival &arr = associatedOrigin->arrivals[index];
		SEISCOMP_INFO(
			"IMP associated pick %s to origin %ld   "
			"phase=%s aff=%.4f dist=%.1f wt=%d",
			pick->id().c_str(), associatedOrigin->id,
			arr.phase.c_str(), arr.affinity, arr.distance,
			arr.excluded ? 0 : 1);
		origin = associatedOrigin;
	}

	// If at this point we already have found an associated origin, which
	// must be an imported origin, we are done.
	if (origin)
		return origin;



	// If no imported origin was found, search for own origins.
	double associatedOriginLargestScore = 0;

	for (auto &a: associations) {

		if ( imported(a.origin.get()) )
			continue;

		// A bit redundant. TODO: Let the associator check this.
		if (a.affinity < _config.minPickAffinity)
			continue;

		OriginPtr associatedOrigin = new Origin(*a.origin.get());

		if (a.phase == "P" || isPKP(a.phase)) {
			std::string oneliner =
				printOneliner(associatedOrigin.get()) +
				"  ph=" + a.phase;
			SEISCOMP_DEBUG_S(" *** " + pick->id());
			SEISCOMP_DEBUG_S(" *** " + oneliner);
			bool success = _associate(
				associatedOrigin.get(), pick, a.phase);

			if (success)
				SEISCOMP_DEBUG_S(" +++ " + oneliner);
			else {
				SEISCOMP_DEBUG_S(" --- " + oneliner);
				// next association
				continue;
			}
		}
		else {
			Arrival arr = a;
			arr.excluded = Arrival::UnusedPhase;
			associatedOrigin->add(arr);
			SEISCOMP_DEBUG_S(" $$$ " + pick->id());
		}

		if ( ! _passedFilter(associatedOrigin.get()))
			continue;

		int index = associatedOrigin->findArrival(pick);
		if (index==-1) {
			THIS_SHOULD_NEVER_HAPPEN;
			return nullptr;
		}
		Arrival &arr = associatedOrigin->arrivals[index];
		SEISCOMP_INFO(
			"associated pick %s to origin %ld   "
			"phase=%s aff=%.4f dist=%.1f wt=%d",
			pick->id().c_str(), associatedOrigin->id,
			arr.phase.c_str(), a.affinity, arr.distance,
			arr.excluded ? 0 : 1);

		int phaseCount = associatedOrigin->definingPhaseCount();
		if (phaseCount > associatedOriginLargestScore) {
			associatedOriginLargestScore = phaseCount;
			origin = associatedOrigin;
		}
	}

	if (origin)
		return origin;

	return nullptr;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Autoloc::DataModel::OriginPtr
Autoloc3::_tryNucleate(const Autoloc::DataModel::Pick *pick)
{
	using namespace Autoloc::DataModel;

	if ( ! _nucleator.feed(pick))
		return nullptr;

	//
	// The following will only be executed if the nucleation of a new
	// origin succeeded.
	//
	// Examine the candidate origins suggested by the nucleator one-by-one
	// The aim is to find an acceptable new origin.
	//
	OriginPtr newOrigin = 0;
	OriginVector candidates = _nucleator.newOrigins();

	SEISCOMP_DEBUG(
		"Autoloc3::_tryNucleate A  candidate origins: %d",
		int(candidates.size()));

	double bestScore = 0;
	for (OriginPtr candidate : candidates) {

		// We are in a dilemma here: We may have a new origin with a
		// bad RMS due to a single outlier or simply bad picks (like
		// for emergent regional Pn). So the origin may actually be
		// resonably good, but the RMS is bad. So. for the very first
		// origin, we allow a somewhat larger RMS. Though "somewhat"
		// has yet to be quantified.

		if (candidate->rms() > 3*_config.maxRMS)
			continue;

		if ( ! newOrigin)
			newOrigin = candidate;
		else {
			double score = _score(candidate.get());
			if (score>bestScore) {
				bestScore = score;
				newOrigin = candidate;
			}
		}
//		break; // HACK

		//
		// We thus only get ONE origin out of the Nucleator!
		// This is *usually* OK, but we might want to try more.
		// TODO: testing needed
		//
	}

	if ( ! newOrigin)
		return nullptr;

	newOrigin->id = _newOriginID();
	newOrigin->arrivals.sort();

	// Try to find the best Origin which might belong to same event
	// TODO avoid the cast...
	Origin *bestEquivalentOrigin = const_cast<Origin*>(
		_origins.bestEquivalentOrigin(newOrigin.get()));
	
	if ( bestEquivalentOrigin != nullptr ) {
 		double rms = bestEquivalentOrigin->rms(),
		       score = _score(bestEquivalentOrigin);

		OriginPtr temp = merge(bestEquivalentOrigin, newOrigin.get());
		if (temp) {
			double epsilon = 1.E-07;
			if (std::abs(temp->rms()-rms)/rms < epsilon &&
			    std::abs(_score(temp.get())-score)/score < epsilon) {

				SEISCOMP_DEBUG_S(
					" MRG " + printOneliner(temp.get()) +
					" UNCHANGED");
			}
			else {
				SEISCOMP_DEBUG_S(
					" MRG " + printOneliner(temp.get()));
				bestEquivalentOrigin->updateFrom(temp.get());
				if ( _passedFilter(bestEquivalentOrigin) )
					return bestEquivalentOrigin;
			}
		}
	}
	else {
		// New origin fresh from the nucleator.
		if ( _passedFilter(newOrigin.get()) )
			return newOrigin;
	}

	return nullptr;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Autoloc::DataModel::OriginPtr
Autoloc3::_tryXXL(const Autoloc::DataModel::Pick *pick)
{
	using namespace Autoloc::DataModel;

	if ( ! _config.xxlEnabled)
		return nullptr;

	OriginPtr origin = _xxlPreliminaryOrigin(pick);
	if ( ! origin)
		return nullptr;

	OriginPtr equivalent = _xxlFindEquivalentOrigin(origin.get());
	if (equivalent) {
		equivalent->updateFrom(origin.get());
		origin = equivalent;
	}

	return origin;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
static size_t countCommonPicks(
// only used in XXL feature
	const Autoloc::DataModel::Origin *origin1,
	const Autoloc::DataModel::Origin *origin2)
{
	using namespace Autoloc::DataModel;

	size_t commonPickCount = 0;
	for (auto &a1: origin1->arrivals) {
		for (auto &a2: origin2->arrivals) {
			if (a1.pick == a2.pick)
				commonPickCount++;
		}
	}

	return commonPickCount;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Autoloc::DataModel::Origin*
Autoloc3::_xxlFindEquivalentOrigin(const Autoloc::DataModel::Origin *origin)
// only used in XXL feature
{
	using namespace Autoloc::DataModel;

	Origin *result = 0;

	for (OriginPtr other : _origins) {
		size_t count = countCommonPicks(origin, other.get());
		if (count >= 3) {
			if (result) {
				if (other->score > result->score)
					result = other.get();
			}
			else
				result = other.get();
		}
	}

	return result;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_process(const Autoloc::DataModel::Pick *pick)
{
	using namespace Autoloc::DataModel;

	if (_expired(pick)) {
		SEISCOMP_INFO_S("ignoring expired pick " + pick->id());
		return false;
	}

	if ( ! valid(pick) ) {
		SEISCOMP_DEBUG("invalid pick %-35s", pick->id().c_str());
		return false;
	}

	if ( automatic(pick) && _tooLowSNR(pick) )
		return false;

	if ( automatic(pick) && _tooManyRecentPicks(pick) ) {
// TODO: Review! Occasionally good picks are ignored
//       especially DL picks with fixed amplitudes
// TODO: Somehow need to take pick priority into account.
		const_cast<Pick*>(pick)->status = Pick::IgnoredAutomatic;
		return false;
	}

	if (pick->blacklisted) {
		SEISCOMP_INFO(
			"process pick %-35s %c blacklisted -> ignored",
			pick->id().c_str(), statusFlag(pick));
		return false;
	}

	_log(pick);

	if ( manual(pick) && ! _config.useManualPicks ) {
		if ( _config.useManualOrigins ) {
			// If we want to consider only associated manual
			// picks, i.e. picks that come along with a manual
			// origin that uses them, we stop here because we
			// don't want to feed it into the associator/nucleator.
			return true;
		}
		else {
			pick->blacklisted = true;
			SEISCOMP_INFO_S(
				"process pick BLACKLISTING " + pick->id() +
				" (manual pick)");
			return false;
		}
	}

	// TODO: review especially in the context of manual picks
	// if ( _followsBiggerPick(pick) )
	//	return false;

	// TODO: review
	// if ( _perhapsPdiff(pick) )
	//	return false;


	// ////////////////////////////////////////////////////////////


	// Now that the pick has passed several filters, it can finally
	// be processed.
	// TODO: Perhaps move the above filters to a subroutine.

	SEISCOMP_INFO(
		"process pick %-35s %s",
		pick->id().c_str(), (pick->xxl ? " XXL" : ""));

	OriginPtr _origin = nullptr;
	Origin *origin = nullptr;

	_origin = _trySupersede(pick);
	origin = _origin.get();
	if ( origin ) {
SEISCOMP_DEBUG_S("_trySupersede succeeded for pick "+pick->id());
		// TODO: take care of updated origin
		_rework(origin);
		if ( _passedFilter(origin) ) {
			_store(origin);
			return true;
		}

		return true;
	}
SEISCOMP_DEBUG_S("_trySupersede failed for pick "+pick->id()+" which is OK.");

	// Try to associate this pick to an existing origin
	_origin = _tryAssociate(pick);
	origin = _origin.get();
	if ( origin ) {
		// After successful association with an imported origin
		// we're done.
		//
		// Explanation:
		//
		// We sometimes observe fake origins due to PKP phases.
		// For instance, Fiji-Tonga PKP in central Europe easily
		// trigger teleseismic picker, but with a network of only
		// central european station we are sometimes unable to
		// properly handle these picks in order to prevent them
		// from resulting in fake origins.
		//
		// An imported origin from another agency, received before
		// the PKP arrives in central Europe, can be used to take
		// care of these PKP's.

		if ( imported(origin) ) {
			// TODO: Review.
			//       At some point we might want to relocate.
			//       or at least report the association.
			_store(origin);
			return true;
		}
		else {
			// Not an imported origin, take the normal route.
			_rework(origin);
			if ( _passedFilter(origin) )
				_store(origin);
			else
				origin = nullptr;
		}
	}

	// If the origin meets certain criteria, we bypass the nucleator.
	if ( origin ) {
		if ( imported(origin) )
			return true;
		if (origin->score >= _config.minScoreBypassNucleator)
			return true;
	}


	// The following will only be executed if the association with an
	// existing origin failed or if the score of the best associated
	// origin s too small.
	//
	// In that case, feed the new pick to the nucleator.
	// The result may be several candidate origins; in a loop we examine
	// each of them until the result is satisfactory.

	if ( origin ) {
		// Feed the pick to the Nucleator but ignore result
		OriginPtr ignore = _tryNucleate(pick);
		return true;
	}

	_origin = _tryNucleate(pick);
	origin = _origin.get();
	if ( origin ) {
		_rework(origin);
		if ( _passedFilter(origin) ) {
			_store(origin);
			return true;
		}
	}


	// If up to now we haven't successfully procesed the new pick,
	// finally try the XXL feature.

	_origin = _tryXXL(pick);
	origin = _origin.get();
	if ( origin ) {
		_rework(origin);
		if ( _passedFilter(origin) ) {
			_store(origin);
			return true;
		}
	}

	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Autoloc::DataModel::OriginID Autoloc3::_findMatchingOriginViaPicks(const Autoloc::DataModel::Origin *origin) const
{
	using namespace Autoloc::DataModel;

	// Find all matching picks and determine if there is an
	// existing origin that some of these picks have been
	// associated to already. We could of course also look for a
	// matching origin but the origin may be inaccurately located
	// so we prefer to go by the matching picks.

	// There is also _findMatchingOrigin but it works a bit
	// differently. TODO: Find a common denominator.

        bool considerDisabledStations =
		associateDisabledStationsToQualifiedOrigin &&
		(imported(origin) || manual(origin));

	AssociationVector associations;
	_associator.findMatchingPicks(origin, associations);
	std::map<OriginID, size_t> associationCount;
	for (const Association &asso: associations) {
		const Pick *pick = asso.pick.get();
SEISCOMP_DEBUG_S("Autoloc3::_findMatchingOriginViaPicks A "+pick->id());

		// We may also want to associate disabled stations
		// to *imported* origins (and from there on).
		if ( ! pick->station()->enabled && ! considerDisabledStations)
			continue;
SEISCOMP_DEBUG_S("Autoloc3::_findMatchingOriginViaPicks B "+pick->id());

		if (pick->priority <= 0)
			continue;
SEISCOMP_DEBUG_S("Autoloc3::_findMatchingOriginViaPicks C "+pick->id());

		const Origin* other = _origins.find(pick->originID());
		OriginID id = other ? other->id : -1;
		size_t count = 0;
		if (other)
			count = ++ associationCount[other->id];
		SEISCOMP_INFO(
			"ASSO %5s %5s  %6.2f %4ld %4ld",
			pick->station()->code.c_str(),
			asso.phase.c_str(),
			asso.residual, id, count);
	}

	SEISCOMP_INFO(
		"ASSO number of origins %4ld",
		associationCount.size());
	OriginID id = -1;
	size_t maxCount = 0;
	for (auto item: associationCount) {
		SEISCOMP_INFO(
			"ASSO matches per origin %4ld %4ld",
			item.first, item.second);
		if (item.second > maxCount) {
			id = item.first;
			maxCount = item.second;
		}
	}

	return id;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_processImportedOrigin(Autoloc::DataModel::Origin *importedOrigin)
{
	using namespace Autoloc::DataModel;

	// This is the entry point for an external origin from a
	// trusted agency for passive association only

	// Find all matching picks and determine if there is an
	// existing origin that some of these picks have been
	// associated to already. We could of course also look for a
	// matching origin but the origin may be inaccurately located
	// so we prefer to go by the matching picks.

        bool considerDisabledStations =
		associateDisabledStationsToQualifiedOrigin &&
		(imported(importedOrigin) || manual(importedOrigin));

	// There is also _findMatchingOrigin but it works a bit
	// differently. TODO: Find a common denominator.
	OriginID id = _findMatchingOriginViaPicks(importedOrigin);

	SEISCOMP_INFO("Autoloc3::_processImportedOrigin: best-match origin %ld", id);

/*
	Origin *bestMatch = nullptr;
	for (auto o: _origins) {
		if (o->id == id) {
			bestMatch = o.get();
			break;
		}
	}
*/
	// Get the origin instance from the id
	Origin *bestMatch = _origins.find(id);

	// Assign the imported origin as reference origin to the
	// best-matching origin.

	// If the best-matching origin either doesn't have a reference
	// origin yet or if it is also an imported origin, update the
	// reference origin. Note that manual origins have priority,
	// therefore we don't override a manual reference origin by
	// an imported origin!

	if (bestMatch) {
		SEISCOMP_INFO_S(" BBB " + printOneliner(bestMatch));
		SEISCOMP_INFO_S(" IMP " + printOneliner(importedOrigin));
		if (bestMatch->referenceOrigin == nullptr)
			bestMatch->referenceOrigin = importedOrigin;
		else {
			if (imported(bestMatch))
				bestMatch->referenceOrigin = importedOrigin;
		}

		importedOrigin->id = bestMatch->id;
	}

	OriginPtr fromMatchingPicks = new Origin(*importedOrigin);
	fromMatchingPicks->arrivals.clear();
	fromMatchingPicks->referenceOrigin = importedOrigin;

	AssociationVector associations;
	_associator.findMatchingPicks(importedOrigin, associations);
	// TODO: add more arrivals to bestMatch origin
	for (auto &a: associations) {

// TODO: Here we also need to check if a pick supersedes another

		if ( ! (a.phase == "P" || isPKP(a.phase)))
			continue;

		const Pick* pick = a.pick.get();

		const Origin* other = _origins.find(pick->originID());
		const char *sta = pick->station()->code.c_str();
		const char *phc = a.phase.c_str();

		if (_requiresAmplitude(pick) && ! hasAmplitude(pick)) {
			// Prevent a pick without amplitudes from being associated
			// TODO: Review if really needed at this point
			continue;
		}


		// associate only picks not yet associated to any other origin
		bool associate = false;
		if ( ! pick->originID())
			associate = true;
		else {
			if ( bestMatch && a.pick->originID() == bestMatch->id)
				associate = true;
		}

		if (associate) {
			Arrival tmp = a;
			tmp.excluded = Arrival::NotExcluded;
			fromMatchingPicks->add(tmp);
			SEISCOMP_INFO(
				"ASSO ADD %5s %5s  %6.2f",
				sta, phc, tmp.residual);
		}
		else {
			SEISCOMP_INFO(
				"ASSO XYZ %5s %5s", sta, phc);
			continue;
		}
	}


//	fromMatchingPicks->manual = true;
	fromMatchingPicks->depthType = Origin::DepthFree;

	// TODO: Look for pP candidates

	// TODO: avoid duplicate arrivals in bestMatch origin

	// TODO: release updated bestMatch origin

	// TODO: relocation threshold based on score

	bool relocateImportedOrigin = false; // TEMP
	if (relocateImportedOrigin) {
		SEISCOMP_DEBUG("Before relocation");

		_relocator.setFixedDepth(importedOrigin->dep);
		_relocator.useFixedDepth(_config.adoptImportedOriginDepth);
//		_relocator.useFixedDepth(true);
		bestMatch->dep = importedOrigin->dep;
		bestMatch->depthType = Origin::DepthFree;
		SEISCOMP_DEBUG_S(" IMP- " + printOneliner(fromMatchingPicks.get()));
//		SEISCOMP_DEBUG_S(printDetailed(fromMatchingPicks.get()));
		OriginPtr relo = _relocator.relocate(fromMatchingPicks.get());
		if ( ! relo) {
			RELOCATION_FAILED_WARNING;
			delete bestMatch;
			return false;
		}

		bestMatch->updateFrom(relo.get());
		bestMatch->referenceOrigin = importedOrigin;

		SEISCOMP_DEBUG_S(" IMP+ " + printOneliner(bestMatch));
		SEISCOMP_DEBUG_S(printDetailed(bestMatch));
		bestMatch->locked = false;
		_rework(bestMatch);
	}
	else {
		bestMatch->updateFrom(fromMatchingPicks.get());
		bestMatch->referenceOrigin = importedOrigin;
		bestMatch->locked = true;  // prevent relocation
	}

	// TODO: Check residuals and include/remove phases based on
	//       residual

	_store(bestMatch);
	report();
	cleanup();
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_processManualOrigin(Autoloc::DataModel::Origin *origin)
{
	using namespace Autoloc::DataModel;
	const Origin *manualOrigin = origin;

	if ( manualOrigin->arrivals.empty() ) {
		SEISCOMP_WARNING("Ignoring manual origin without arrivals");
		return false;
	}

	SEISCOMP_INFO(
		"processing manual origin z=%.3fkm   dtype=%d",
		manualOrigin->dep, manualOrigin->depthType);

	// Look for a matching (autoloc) origin. Our intention is to find the
	// best-matching origin and merge it with the just received manual
	// origin (adopt picks, fixed focal depth etc.)

	// FIXME: Only origins in memory are found, but not origins in database!
	OriginID id = _findMatchingOrigin(manualOrigin);

	Origin *found = nullptr;

	if (found) {
		SEISCOMP_DEBUG(
			"found matching origin with id=%ld  z=%.3fkm",
			found->id, found->dep);

		// update existing origin with information from received origin
		ArrivalVector arrivals;

		for (auto &a: manualOrigin->arrivals) {
			if ( ! a.pick->station())
				continue;
			arrivals.push_back(a);
		}

		// merge origin
		for (auto &a: found->arrivals) {
			if ( ! a.pick->station()) {
				THIS_SHOULD_NEVER_HAPPEN;
				SEISCOMP_ERROR("Pick is %s without station",
					       a.pick->id().c_str());
				continue;
			}

			// Do we have an arrival for this station already?
			// We have to look for arrivals that either reference
			// the same pick or arrivals for the same station/phase
			// combination. The latter is still risky if two
			// nearby picks of the same onset are assigned
			// different phase codes, e.g. P/Pn or P/PKP; in that
			// case we end up with both picks forming part of the
			// solution.
			bool have = false;
			for (auto &_a: arrivals) {
				if (_a.pick == a.pick) {
					have = true;
					break;
				}

				if (_a.pick->station() == a.pick->station()
				    && _a.phase == a.phase) {
					have = true;
					break;
				}
			}

			if (have)
				continue;

			arrivals.push_back(a);
		}
		arrivals.sort();

		*found = *manualOrigin;
		found->arrivals = arrivals;
		found->id = id;

		switch (manualOrigin->depthType) {
		case Origin::DepthManuallyFixed:
			_relocator.useFixedDepth(true);
			break;
		case Origin::DepthPhases:
		case Origin::DepthFree:
		default:
			_relocator.useFixedDepth(false);
		}

		// TODO: consider making this relocation optional
		OriginPtr relo = _relocator.relocate(found);
		if (relo) {
			found->updateFrom(relo.get());
			_store(found);
			report();
			cleanup();
		}
		else {
			RELOCATION_FAILED_WARNING;
			return false;
		}
	}
	else {
		SEISCOMP_DEBUG("No matching origin found");
		SEISCOMP_DEBUG("Proceeding with manual origin");

		// This code is redundant, see above for the imported origins
		_store(origin);
		AssociationVector associations;
		_associator.findMatchingPicks(origin, associations);
		for (auto &a: associations) {
			const char *sta = a.pick->station()->code.c_str();
			const char *phc = a.phase.c_str();
			SEISCOMP_INFO("ASSO %5s %5s  %6.2f", sta, phc, a.residual);
		}
		return true;
	}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_processQualifiedOrigin(Autoloc::DataModel::Origin *origin)
{
	using namespace Autoloc::DataModel;
	const Origin *qualifiedOrigin = origin;

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<



// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
static int depthPhaseCount(Autoloc::DataModel::Origin *origin)
{
	using namespace Autoloc::DataModel;

	int _depthPhaseCount = 0;
	for (auto &a: origin->arrivals) {
		if ( a.excluded )
			continue;
		if ( a.phase == "pP" || a.phase == "sP" )
			_depthPhaseCount++;
	}
	return _depthPhaseCount;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_setDefaultDepth(Autoloc::DataModel::Origin *origin)
// Set origin depth to the configured default depth and relocate.
// May be set in an origin far outside the network where depth resolution
// is expected to be poor, or in testing that depth resolution.
{
	using namespace Autoloc::DataModel;

	OriginPtr test = new Origin(*origin);

	_relocator.setFixedDepth(_config.defaultDepth);
	_relocator.useFixedDepth(true);
	LOG_RELOCATOR_CALL;
	OriginPtr relo = _relocator.relocate(test.get());
	if ( ! relo) {
		SEISCOMP_WARNING("_setDefaultDepth: failed relocation");
		return false;
	}

	origin->updateFrom(relo.get());
	origin->depthType = Origin::DepthDefault;

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<



// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_setTheRightDepth(Autoloc::DataModel::Origin *origin)
{
	using namespace Autoloc::DataModel;

	if ( ! _config.tryDefaultDepth)
		return false;

	if (origin->depthType == Origin::DepthPhases)
		return false;

	// TODO: XXX XXX XXX
	// Dann aber auch mal testen, ob man mit freier Tiefe evtl. weiter
	// kommt. Sonst bleibt das immer bei der Default-Tiefe haengen!
	if (origin->depthType == Origin::DepthDefault) {
		// TODO: improve
		OriginPtr test = new Origin(*origin);
		test->depthType = Origin::DepthFree;

		_relocator.useFixedDepth(false);
		OriginPtr relo = _relocator.relocate(test.get());
		if ( ! relo) {
			SEISCOMP_WARNING(
				"_setDefaultDepth: failed relocation");
			return false;
		}

		double radius = 5*(relo->dep >= _config.defaultDepth ?
				   relo->dep : _config.defaultDepth)/111.195;
		
		// This is a hack, but in practice works pretty well:
		// If there are at least 2 stations within 5 times the
		// source depth, we assume sufficient depth resolution.
		if (relo->definingPhaseCount(0, radius) >= 2) {
			origin->updateFrom(relo.get());
			return false;
		}

		return true;
		// XXX BAUSTELLE XXX
	}

	// This is a hack, but in practice works pretty well:
	// If there are at least 2 stations within 5 times the
	// source depth, we assume sufficient depth resolution.
	if (origin->definingPhaseCount(0, (5*origin->dep)/111.2) >= 2)
		return false;

	OriginPtr test = new Origin(*origin);
	if ( ! _setDefaultDepth(test.get()))
		return false; // relocation using default depth failed

	// test origin now has the default depth (fixed)


	// Regarding the default depth "stickiness", we currently
	// distinguish three cases:
	//
	// stickiness >= 0.9: force use of default depth;
	//    might make a deep origin unrelocatable!
	// 0.1 < stickiness < 0.9: try default depth vs. free depth
	// stickiness <= 0.1 never use default depth - TODO

	if (_config.defaultDepthStickiness < 0.9) {
		// only then we need to try another depth

		double rms1 = origin->rms();      // current rms
		double rms2 = test->rms();        // rms with z=default

		// if setting z=default increases the rms "significantly"...
		if ( rms2 > 1.2*rms1 && rms2 > _config.goodRMS ) {
#ifdef EXTRA_DEBUGGING
			SEISCOMP_DEBUG(
				"_testDepthResolution good for origin %ld "
				"(rms criterion)", origin->id);
#endif
			return false;
		}


		double score1 = _score(origin);      // current score
		double score2 = _score(test.get());  // score with z=default

		// if setting z=default decreases the score "significantly"...
		if ( score2 < 0.9*score1-5 ) {
#ifdef EXTRA_DEBUGGING
			SEISCOMP_DEBUG(
				"_testDepthResolution good for origin %ld "
				"(score criterion)", origin->id);
#endif
			return false;
		}

		if (origin->dep != test->dep)
			SEISCOMP_INFO(
				"Origin %ld: changed depth from %.1f to "
				"default of %.1f   score: %.1f -> %.1f   "
				"rms: %.1f -> %.1f",
				origin->id, origin->dep, test->dep,
				score1, score2, rms1, rms2);
	}

	origin->updateFrom(test.get());
	origin->depthType = Origin::DepthDefault;
	_updateScore(origin); // why here?

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_expired(const Autoloc::DataModel::Pick *pick) const
{
	return pick->time < _now - _config.maxAge;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_requiresAmplitude(const Autoloc::DataModel::Pick *pick) const
{
	// TODO: Review as there may be automatic picks that don't
	// require all amplitudes.

	if (pick->scpick->methodID() == "DL")
		// XXX TEMP HACK XXX for deep-learning picks XXX
		return false;

	return automatic(pick);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_epicenterRequiresDefaultDepth(
	const Autoloc::DataModel::Origin * /* origin */ ) const
{
	using namespace Autoloc::DataModel;

	// TODO ;)
	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::_ensureConsistentArrivals(Autoloc::DataModel::Origin *origin)
{
	using namespace Autoloc::DataModel;

	// ensure consistent distances/azimuths of the arrivals
	for (auto &a: origin->arrivals) {
		double delta, az, baz;
		delazi(origin, a.pick->station(), delta, az, baz);
		a.distance = delta;
		a.azimuth = az;
	}

	origin->arrivals.sort();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::_ensureAcceptableRMS(Autoloc::DataModel::Origin *origin, bool keepDepth)
{
	using namespace Autoloc::DataModel;

	int minPhaseCount = 20; // TODO: make this configurable

	if (origin->definingPhaseCount() < minPhaseCount)
		return;

	if (origin->rms() <= _config.maxRMS)
		return;

	SEISCOMP_DEBUG("_ensureAcceptableRMS rms loop begin");
	
	while (origin->rms() > _config.maxRMS) {
		SEISCOMP_DEBUG(
			"_ensureAcceptableRMS rms loop %.2f > %.2f",
			origin->rms(), 0.9*_config.maxRMS);

		int definingPhaseCount = origin->definingPhaseCount();

		if (definingPhaseCount < minPhaseCount)
			break;

		// TODO: make this configurable?
		if (definingPhaseCount < 50) {
			// instead of giving up, try to enhance origin
			// This is rather costly, so we do it only up
			// to 50 defining picks, as then usually the
			// solution is so consolidated that switching
			// to removal of the pick with largest residual
			// is a safe bet.
			if ( ! _enhanceScore(origin, 1))
				break;
		}
		else {
			int worst = arrivalWithLargestResidual(origin);
			origin->arrivals[worst].excluded =
				Arrival::LargeResidual;
			_relocator.useFixedDepth(keepDepth ? true : false);
			LOG_RELOCATOR_CALL;
			OriginPtr relo = _relocator.relocate(origin);
			if ( ! relo) {
				SEISCOMP_WARNING(
					"Relocation failed in "
					"_ensureAcceptableRMS for origin %ld",
					origin->id);
				break;
			}
			origin->updateFrom(relo.get());
		}
	}

	SEISCOMP_DEBUG("_ensureAcceptableRMS rms loop end");
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::_updateScore(Autoloc::DataModel::Origin *origin)
{
	using namespace Autoloc::DataModel;

	origin->score = _score(origin);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_rework(Autoloc::DataModel::Origin *origin)
{
	using namespace Autoloc::DataModel;

#ifdef EXTRA_DEBUGGING
	SEISCOMP_DEBUG("_rework begin   deperr=%.1f", origin->deperr);
#endif
	// This is the minimum requirement
	if (origin->definingPhaseCount() < _config.minPhaseCount)
		return false;

	// There are several possible conditions that may require use of
	// the default depth for this origin. Check if any of these is met.
	bool enforceDefaultDepth = false;
	bool adoptManualDepth = false;


	// Remove arrivals with blacklisted picks
	ArrivalVector arrivals;
	for (auto &a: origin->arrivals) {
		if (a.excluded & Arrival::BlacklistedPick)
			continue;
		arrivals.push_back(a);
	}
	origin->arrivals = arrivals;


	// TODO: put all this depth related stuff into _setTheRightDepth()
	if (_config.adoptManualDepth && (
			origin->depthType == Origin::DepthManuallyFixed ||
			origin->depthType == Origin::DepthPhases ))
	{
			SEISCOMP_INFO(
				"Adopting depth of %g km from manual origin",
				origin->dep);
			adoptManualDepth = true;
	}
	else if ( imported(origin) ) {
		if (_config.adoptImportedOriginDepth) {
			SEISCOMP_INFO(
				"Adopting depth of imported origin "
				"of %g km", origin->referenceOrigin->dep);
			adoptManualDepth = true;
		}
		else {
			SEISCOMP_INFO(
				"Free depth due to depth of imported origin "
				"of %g km", origin->referenceOrigin->dep);
			adoptManualDepth = false;
		}

		_relocator.setFixedDepth(origin->referenceOrigin->dep);
		_relocator.useFixedDepth(_config.adoptImportedOriginDepth);
	}
	else {
		// TODO: XXX REVIEW XXX URGENT XXX
		if ( _config.defaultDepthStickiness >= 0.9 ) {
			enforceDefaultDepth = true;
			SEISCOMP_INFO(
				"Enforcing default depth due to stickiness");
		}
		else if (_epicenterRequiresDefaultDepth(origin) &&
			 _setDefaultDepth(origin) )
		{
			enforceDefaultDepth = true;
			SEISCOMP_INFO(
				"Enforcing default depth due to "
				"epicenter location");
		}
		else if ( _setTheRightDepth(origin) ) {
			enforceDefaultDepth = true;
			SEISCOMP_INFO(
				"Enforcing default depth due to "
				"epicenter-station geometry");
		}
		else
			SEISCOMP_INFO("Not fixing depth");
	}

	// The _enhance_score() call is slow for origins with many phases,
	// while the improvement becomes less. So at some point, we don't
	// want to call _enhance_score() too often or not at all.
	// TODO: make this configurable
	if (origin->definingPhaseCount() < 30)
		_enhanceScore(origin);

	if (enforceDefaultDepth)
		_relocator.setFixedDepth(_config.defaultDepth);

	bool keepDepth = adoptManualDepth || enforceDefaultDepth;

	_relocator.useFixedDepth(keepDepth ? true : false);
	_trimResiduals(origin);  // calls _relocator

	// Only use stations up to _config.maxStaDist
	// TODO: need to review this...
	while (origin->definingPhaseCount(0, _config.maxStaDist) >
	       _config.minPhaseCount)
	{
		int arrivalCount = origin->arrivals.size();
		double dmax=0;
		int imax=-1;
		// find the farthest used station
		for (int i=0; i<arrivalCount; i++) {

			Arrival &a = origin->arrivals[i];
			if (a.excluded)
				continue;
			if (a.distance > dmax) {
				dmax = a.distance;
				imax = i;
			}
		}

		Arrival &a = origin->arrivals[imax];
		if (a.distance < _config.maxStaDist)
			break;
		a.excluded = Arrival::StationDistance;

		// relocate once
		LOG_RELOCATOR_CALL;
		OriginPtr relo = _relocator.relocate(origin);
		if ( ! relo) {
			SEISCOMP_WARNING(
				"A relocation failed in _rework for "
				"origin %ld", origin->id);
			break;
		}

		origin->updateFrom(relo.get());
	}

	_ensureAcceptableRMS(origin, keepDepth);
	_addMorePicks(origin, keepDepth);

	_trimResiduals(origin); // again!
	_removeWorstOutliers(origin);
	_excludeDistantStations(origin);
	_excludePKP(origin);

	if (origin->dep != _config.defaultDepth &&
	    origin->depthType == Origin::DepthDefault)
		origin->depthType = Origin::DepthFree;

	// once more (see also above)
	if (origin->definingPhaseCount() < _config.minPhaseCount) {
		return false;
	}

	// Make sure that we adopt all picks associated to this origin,
	// which are not yet adopted.
	for (auto &a: origin->arrivals) {
//		if (a.excluded)
//			continue;

		const Pick *pick = a.pick.get();

		// TODO:
		// It would make sense to check if the pick is already
		// adopted my another origin with higher score or to which
		// the affinity is higher. This would be in order to prevent
		// "stealing" of picks.

		// if (isTrackedPick(pick->id())) {
		if (true) {
			if (pick->originID()) {
				if (pick->originID() != origin->id) {
					SEISCOMP_INFO(
						"Pick %s stolen from "
						"origin %ld by "
						"origin %ld",
						pick->id().c_str(),
						pick->originID(),
						origin->id);
				}
			}
			else {
				SEISCOMP_INFO(
					"Pick %s adopted by origin %ld",
					pick->id().c_str(), origin->id);
			}
		}

		origin->adopt(pick);
	}

#ifdef EXTRA_DEBUGGING
	SEISCOMP_DEBUG("_rework end");
#endif
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_excludePKP(Autoloc::DataModel::Origin *origin)
{
	using namespace Autoloc::DataModel;
SEISCOMP_DEBUG("_excludePKP");
	bool relocate = false;
	for (auto &a: origin->arrivals) {
		if (a.excluded)
			continue;
		if (a.distance < 105)
			continue;
		// TODO: how about PKiKP?

		if (isP(a.phase) || isPKP(a.phase)
		    /* || arr.phase == "PKiKP" */ ) {
			// for times > 960, we expect P to be PKP
			if (a.pick->time - origin->time > 960) {
				a.excluded = Arrival::UnusedPhase;
SEISCOMP_DEBUG_S("_excludePKP pick="+a.pick->id());
				relocate = true;
			}
		}
	}

	if ( ! relocate)
		return false;

	// relocate once
	LOG_RELOCATOR_CALL;
	OriginPtr relo = _relocator.relocate(origin);
	if ( ! relo) {
		SEISCOMP_WARNING(
			"A relocation failed in _excludePKP for origin %ld",
			origin->id);
		return false;
	}

	origin->updateFrom(relo.get());

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_excludeDistantStations(Autoloc::DataModel::Origin *origin)
{
	using namespace Autoloc::DataModel;

	double q = 4;
	std::vector<double> distance;

	for (auto &a: origin->arrivals) {
		// ignore excluded arrivals except those that were previously
		// excluded because of the distance criterion, because the
		// latter may no longer hold (i.e. more distant stations)
		if (a.excluded && a.excluded != Arrival::StationDistance)
			continue;
		// ignore PKP, *may* be a bit risky -> checks required!
		if (a.distance > 110)
			continue;
		distance.push_back(a.distance);
	}
	int distanceCount = distance.size();
	if (distanceCount < 4)
		return false;

	sort(distance.begin(), distance.end());

	int nx = 0.1*distanceCount > 2 ? int(0.1*distanceCount) : 2;
//	double medDistance=Seiscomp::Math::Statistics::median(distance);
	double maxDistance=distance[distanceCount-nx];

	for (int i=distanceCount-nx+1; i<distanceCount; i++) {
		if(distance[i] > q*maxDistance)
			break;
		maxDistance = distance[i];
	}

	int excludedCount = 0;
	for (auto &a: origin->arrivals) {
		if (a.excluded)
			continue;
		if (a.distance > maxDistance) {
			a.excluded = Arrival::StationDistance;
			excludedCount++;
			SEISCOMP_DEBUG(
				"_excludeDistantStations origin %ld exc %s",
				origin->id, a.pick->id().c_str());
		}
	}
	if (excludedCount) {
		LOG_RELOCATOR_CALL;
		OriginPtr relo = _relocator.relocate(origin);
		if (relo) {
			origin->updateFrom(relo.get());
			return true;
		}
	}

	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_passedFilter(Autoloc::DataModel::Origin *origin)
{
	using namespace Autoloc::DataModel;

	if (_config.offline || _config.test) {
		SEISCOMP_DEBUG_S(" TRY " + printOneliner(origin));
		SEISCOMP_DEBUG_S(printDetailed(origin));
	}

/*
	//////////////////////////////////////////////////////////////////
	// new distance vs. min. pick count criterion
	int arrivalCount = origin->arrivals.size();
	int phaseCount = origin->definingPhaseCount();
	int consistentPhaseCount = 0;
	for (int i=0; i<arrivalCount; i++) {

		Arrival &a = origin->arrivals[i];
		if (a.excluded)
			continue;
		if ( ! isP(a.phase) && ! isPKP(a.phase))
			continue;

		// compute min. phase count of origin for this pick to be consistent with that origin
		int minPhaseCount = _config.minPhaseCount + (a.distance-a.pick->station()->maxNucDist)*_config.distSlope;

		SEISCOMP_DEBUG(" AAA origin=%d pick=%s  %d  %d", origin->id,
		a.pick->id().c_str(), phaseCount, minPhaseCount);
		if (phaseCount < minPhaseCount) {
//			if (_config.offline || _config.test)
				SEISCOMP_DEBUG(" XXX inconsistent origin=%d pick=%s", origin->id, arr.pick->id().c_str());
			continue;
		}

		consistentPhaseCount++;
	}
	if (consistentPhaseCount < _config.minPhaseCount) {
//		if (_config.offline || _config.test)
			SEISCOMP_DEBUG_S(" XXX " + printOneliner(origin));
		return false;
	}
	//////////////////////////////////////////////////////////////////
*/

	double fakeProbability = _testFake(origin);
	if (fakeProbability > _config.maxAllowedFakeProbability) {
		SEISCOMP_DEBUG_S(printDetailed(origin));
		SEISCOMP_DEBUG(
			"Probable fake origin: %ld - prob=%.3f",
			origin->id, fakeProbability);
		return false;
	}

	if ( ! _passedFinalCheck(origin))
		return false;

	_ensureConsistentArrivals(origin);

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_polish(Autoloc::DataModel::Origin *origin)
{
	using namespace Autoloc::DataModel;

	_rename_P_PKP(origin);

	_updateScore(origin);

	if (depthPhaseCount(origin))
		origin->depthType = Origin::DepthPhases;

	if (origin->depthType == Origin::DepthDefault &&
	    origin->dep != _config.defaultDepth)
		origin->depthType = Origin::DepthFree;

	// TODO: Review
	if ( ! imported(origin) &&
	     origin->definingPhaseCount() >= _config.minPhaseCount)
		origin->preliminary = false;

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_store(Autoloc::DataModel::Origin *origin)
{
	using namespace Autoloc::DataModel;

	_polish(origin);

	Origin *existing = _origins.find(origin->id);
	if (existing) {
		existing->updateFrom(origin);
		origin = existing;
		SEISCOMP_INFO_S(" UPD " + printOneliner(origin));
	}
	else {
		SEISCOMP_INFO_S(" NEW " + printOneliner(origin));
		_origins.push_back(origin);
	}

	// Some additional log output only if we don't send the origin.
	if (_config.offline || _config.test)
		SEISCOMP_DEBUG_S(printDetailed(origin));

	// See if any of the tracked picks is referenced by origin and
	// if so, print a log line.
	for (auto &a: origin->arrivals) {
		if (isTrackedPick(a.pick->id())) {
			SEISCOMP_INFO_S("Origin references pick "+a.pick->id());
			SEISCOMP_INFO("  phase:    %s", a.phase.c_str());
			SEISCOMP_INFO("  residual: %.3f", a.residual);
//			SEISCOMP_INFO("  affinity: %.3f", a.affinity);
			SEISCOMP_INFO("  excluded: %s", a.excluded ? "yes":"no");
		}
	}

	if (_newOrigins.find(origin) == false)
		_newOrigins.push_back(origin);

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_associate(
	Autoloc::DataModel::Origin *origin,
	const Autoloc::DataModel::Pick *pick,
	const std::string &phase)
{
	using namespace Autoloc::DataModel;
if (isPKP(phase)) SEISCOMP_DEBUG("_associate PKP?? A");

	const std::string &pickID = pick->id();

	// TODO: See how we can get rid of this restriction
	if ( ! isP(phase) && ! isPKP(phase)) {
		if (isTrackedPick(pickID))
			SEISCOMP_DEBUG_S("Pick "+pickID+" is neither P nor PKP");
		return false;
	}

if (isPKP(phase)) SEISCOMP_DEBUG("_associate PKP?? B");
	if ( ! _associator.mightBeAssociated(pick, origin))
		return false;

if (isPKP(phase)) SEISCOMP_DEBUG("_associate PKP?? C");
	// PKP arrival is always > O.T. + 960 s
	if (isPKP(phase) && pick->time-origin->time < 960) {
		if (isTrackedPick(pickID))
			SEISCOMP_DEBUG_S("Pick "+pickID+" is PKP but tt < 960 s");
		return false;
	}

if (isPKP(phase)) SEISCOMP_DEBUG("_associate PKP?? D");
	const Station* station = pick->station();

	// If pick is already associated to this origin, then nothing to do.
	if (origin->findArrival(pick) >= 0) {
		if (isTrackedPick(pickID))
			SEISCOMP_DEBUG_S("Pick "+pickID+" already associated with this origin");
		return false;
	}

if (isPKP(phase)) SEISCOMP_DEBUG("_associate PKP?? E");
	TravelTime tt;
// FIXME: Problem with generic phases: phase may be PKPab but we compute
// traveltime for generic PKP and hence get PKPdf or PKPbc.
// Currently hack to at least compute PKPab properly. TODO: Review!
	if (isPKP(phase)) {
		// TODO: optionally use generic PKP here
		if ( ! travelTime(origin, station, phase, tt)) {
			if (isTrackedPick(pickID))
				SEISCOMP_DEBUG_S("Pick "+pickID+" is PKP but failed to compute tt");
			return false;
		}
	}
	else if (isP(phase)) {
		if ( ! travelTime(origin, station, "P1", tt)) {
			if (isTrackedPick(pickID))
				SEISCOMP_DEBUG_S("Pick "+pickID+" is P1 but failed to compute tt");
			return false;
		}
	}
	else {
		if ( ! travelTime(origin, station, phase, tt)) {
			if (isTrackedPick(pickID))
				SEISCOMP_DEBUG_S("Pick "+pickID+": failed to compute tt");
			return false;
		}
	}


	double residual = pick->time - origin->time - tt.time;
if (isPKP(phase)) SEISCOMP_DEBUG("_associate PKP?? F res=%.3f phase=%s", residual, tt.phase.c_str());
	Arrival arr(pick, phase, residual);
	if ( ! _residualWithinAllowedRange(arr, 0.9, 1.3)) {
		if (isTrackedPick(pickID))
			SEISCOMP_DEBUG_S("Pick "+pickID+": residual out of range");
		return false;
	}

if (isPKP(phase)) SEISCOMP_DEBUG("_associate PKP?? G");
	// If it is a very early origin, the location may not be
	// very good (esp. depth!) and the residual not very
	// meaningful as criterion.

	double delta, az, baz;
	delazi(origin, station, delta, az, baz);

	// Make the minimum phase count depend linearly on distance
	// TODO: URGENT review
	int minPhaseCount = _config.minPhaseCount
		+ (delta - station->maxNucDist)*_config.distSlope;
	if (origin->phaseCount() < minPhaseCount) {
		if (isPKP(phase) && _config.aggressivePKP) {
			// This is a common case in Europe
			// 
			// E.g. small Fiji-Tonga events not picked at
			// many stations within P range, but recorded
			// at many stations in Europe (PKP caustic).
			// In that case the minPhaseCount criterion
			// might be counter productive, prevent the
			// association as PKP and ultimately result in
			// fake events.
#ifdef EXTRA_DEBUGGING
			SEISCOMP_DEBUG_S(
				"aggressive PKP association for station " +
				station->code);
#endif
		}
		else {
#ifdef EXTRA_DEBUGGING
			SEISCOMP_DEBUG(
				"_associate origin=%ld pick=%s weakly "
				"associated because origin.phaseCount=%d "
				"< minPhaseCount=%d",
				origin->id, pick->id().c_str(),
				origin->phaseCount(), minPhaseCount);
#endif

//			return false;
			arr.excluded = Arrival::TemporarilyExcluded;
		}
	}
if (isPKP(phase)) SEISCOMP_DEBUG("_associate PKP?? H");

	// passive association to an imported origin
	if (imported(origin) && origin->locked) {
		// If the origin is locked we leave it as it is!
		// arr.excluded = Arrival::UnusedPhase;
	}

	arr.distance = delta;
	arr.azimuth  = az;

	// Disable stations beyond a certain distance.
	// TODO: Make that distance configurable.
	double maxStationDelta = 105;
	if ( ! origin->locked) {
		if (arr.phase != "P")
			arr.excluded = Arrival::UnusedPhase;
		if (arr.distance > maxStationDelta)
			arr.excluded = Arrival::StationDistance;
	}
	// Note that we used to be able to theoretically create a
	// location based only on PKP but this wasn't needed by anyone.

	OriginPtr copy = new Origin(*origin);
	double original_score = _score(copy.get());
	double original_rms   = copy->rms();

	// Now the actual association
	SEISCOMP_DEBUG_S(
		" ADD " + printOneliner(origin) +
		" add " + arr.pick->id() + " " + arr.phase);
	SEISCOMP_DEBUG("res = %.3f", arr.residual);
	copy->add(arr);

// TEMP	
SEISCOMP_DEBUG_S(
" ADD " + printDetailed(copy.get()));

	if (origin->locked) {
		origin->updateFrom(copy.get());
		// We're done because origin is locked
		return true;
	}
if (isPKP(phase)) SEISCOMP_DEBUG("_associate PKP?? J");


	// Relocate etc.
	// TODO: Review and simplify.
	OriginPtr relo = 0;
	if ( arr.excluded != Arrival::UnusedPhase) {

		// score and rms before relocation
//		double score2 = _score(copy.get());
//		double rms2   = copy->rms();

		// Relocate and test if score improves, otherwise
		// leave pick only loosely associated.
		// TODO: Check for RMS improvement as well.

		// Relocate once with fixed depth and
		// in case of failure use free depth
		bool fixed = false;
		if (_config.defaultDepthStickiness > 0.9) {
			fixed = true;
			_relocator.setFixedDepth(_config.defaultDepth);
		}

//		else if (origin->depthType == Origin::DepthManuallyFixed || origin->depthType == Origin::DepthPhases) {
		else if (origin->depthType == Origin::DepthManuallyFixed) {
			fixed = true;
			_relocator.setFixedDepth(origin->dep);
		}
		_relocator.useFixedDepth(fixed);
		LOG_RELOCATOR_CALL;
		relo = _relocator.relocate(copy.get());
		if ( ! relo ) {
			if ( fixed )
				return false;
			else {
				_relocator.setFixedDepth(origin->dep);
				_relocator.useFixedDepth(true);
				LOG_RELOCATOR_CALL;
				relo = _relocator.relocate(copy.get());
				if ( ! relo)
					// 2nd failed relocation
					return false;
			}
		}

		// score/rms after relocation
		double score2 = _score(relo.get());
		double rms2   = relo->rms();
#ifdef EXTRA_DEBUGGING
		SEISCOMP_DEBUG(
			"_associate trying pick id=%s  as %s   res=%.1f",
		       arr.pick->id().c_str(), phase.c_str(), arr.residual);
		SEISCOMP_DEBUG(
			"_associate score: %.1f -> %.1f    rms: %.2f -> %.2f ",
		       original_score, score2, original_rms, rms2);
#endif
		if (score2 < original_score ||
		    rms2 > original_rms + 3./sqrt(10.+copy->arrivals.size())) {
			// no improvement

			int index = copy->findArrival(pick);
			if (index==-1) {
				THIS_SHOULD_NEVER_HAPPEN;
				SEISCOMP_ERROR("Could not find arrival in _associate");
				SEISCOMP_ERROR("Pick is %s", pick->id().c_str());
				return false;
			}
			Arrival &arr = copy->arrivals[index];
			arr.excluded = Arrival::LargeResidual;

			// relocate anyway, to get consistent residuals even
			// for the unused picks
#ifdef EXTRA_DEBUGGING
			SEISCOMP_DEBUG(
				"_associate trying again with fixed depth "
				"of z=%g", origin->dep);
#endif
			_relocator.setFixedDepth(origin->dep);
			_relocator.useFixedDepth(true);
			LOG_RELOCATOR_CALL;
			relo = _relocator.relocate(copy.get());
			if ( ! relo) {
				THIS_SHOULD_NEVER_HAPPEN;
				SEISCOMP_ERROR("Failed relocation in _associate");
			}
			else {
				// score after relocation
				double score3 = _score(relo.get());
#ifdef EXTRA_DEBUGGING
				double rms3 = copy->rms();
				SEISCOMP_DEBUG(
					"_associate score: %.1f -> %.1f -> %.1f   "
					"rms: %.2f -> %.2f -> %.2f",
					original_score, score2, score3,
					original_rms, rms2, rms3);
#endif
				// if still no improvement
				if (score3 < original_score)
					relo = 0;
			}
		}

		if (relo) {
			int index = relo->findArrival(pick);
			bool found = index >= 0;
			if ( ! found) {
				THIS_SHOULD_NEVER_HAPPEN;
				SEISCOMP_ERROR("Could not find arrival in _associate");
				SEISCOMP_ERROR("Pick is %s", pick->id().c_str());
				return false;
			}

			Arrival &arr = relo->arrivals[index];
			if (std::abs(arr.residual) > _config.maxResidualUse) {
				// Added arrival but pick is not used
				// due to large residual.
				arr.excluded = Arrival::LargeResidual;
				origin->add(arr);
				return true;
			}
		}
	}

	if (relo) {
		origin->updateFrom(relo.get());
	}
	else {
		copy = new Origin(*origin);
		if (arr.excluded != Arrival::UnusedPhase)
			arr.excluded = Arrival::DeterioratesSolution;
		copy->add(arr);
		origin->updateFrom(copy.get());
	}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_addMorePicks(
	Autoloc::DataModel::Origin *origin,
	bool /* keepDepth */)
{
	// Associate matching picks that have not been associated before.

	using namespace Autoloc::DataModel;

	std::set<std::string> have;
	for (auto &a: origin->arrivals) {
		if (a.excluded)
			continue;

		const Pick *pick = a.pick.get();
		if ( ! pick->station() )
			continue;
		std::string x =
			pick->station()->net + "." +
			pick->station()->code + ":" + a.phase;
		have.insert(x);
	}

	SEISCOMP_DEBUG("_addMorePicks begin");

        bool considerDisabledStations =
		associateDisabledStationsToQualifiedOrigin &&
		(imported(origin) || manual(origin));

	AssociationVector associations;
	_associator.findMatchingPicks(origin, associations);

	size_t picksAdded = 0;

	std::map<OriginID, size_t> associationCount;
	for (const Association &a: associations) {

		const Pick *pick = a.pick.get();

		// FIXME: This check is also made by the associator and
		// shouldn't be duplicated.
		if ( ! pick->station()->enabled && ! considerDisabledStations)
			continue;

		if (manual(pick)) {
			// TODO: review: maybe not needed here
			if ( !_config.useManualPicks)
				continue;
		}
		else {
			if ( ! pick->station()->enabled && ! considerDisabledStations)
				continue;
		}

SEISCOMP_DEBUG_S("_addMorePicks try "+pick->id());
		if (pick->priority <= 0)
			continue;
SEISCOMP_DEBUG_S("_addMorePicks ass "+pick->id());

		bool success = _associate(
			origin, pick, a.phase);
		if (success) {
			SEISCOMP_WARNING(
				"Associated pick %s to origin %ld",
				pick->id().c_str(), origin->id);
		}
		else {
			SEISCOMP_WARNING(
				"Failed to associate pick %s to origin %ld",
				pick->id().c_str(), origin->id);
			continue;
		}
/*
		if (pick->time - origin->time > 960) {
			// FIXME: generic PKP
			if ( ! _associate(origin, pick, "PKP"))
				continue;
		}
		else {
			if ( ! _associate(origin, pick, "P1"))
				continue;
		}
*/

		picksAdded++;
	}

/*
	for (const auto &item: pickPool) {
		const Pick *pick = item.second.get();

		if ( ! pick->station())
			continue;

		if (ignored(pick))
			continue;

		if (manual(pick)) {
			// TODO: review: maybe not needed here
			if ( !_config.useManualPicks)
				continue;
		}
		else {
			if ( ! pick->station()->enabled)
				continue;
		}

		// check if for that station we already have a P/PKP pick
		std::string x = pick->station()->net + "." +
			        pick->station()->code + ":";
		if (have.count(x+"P") || have.count(x+"PKP"))
			continue;

		if (pick->amp <= 0. || pick->snr <= 0.)
			continue;

		if (_tooLowSNR(pick))
			continue;

		if (pick->blacklisted)
			continue;

		// associated to another origin?
		// TODO: review: check with what weight!
		if (pick->origin())
			continue;

		if (pick->time - origin->time > 960) {
			// FIXME: generic PKP
			if ( ! _associate(origin, pick, "PKP"))
				continue;
		}
		else {
			if ( ! _associate(origin, pick, "P1"))
				continue;
		}

		picksAdded++;
	}
*/
	SEISCOMP_DEBUG("_addMorePicks end   added %ld", picksAdded);

	return picksAdded ? true : false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_enhanceScore(Autoloc::DataModel::Origin *origin, size_t maxloops)
{
	using namespace Autoloc::DataModel;

	// TODO: make sure that the RMS doesn't increase too badly!
	size_t count = 0, loops = 0;

	// a very early origin
	if (origin->definingPhaseCount() < 1.5*_config.minPhaseCount) {

		// count XXL picks
		size_t xxlcount = 0;
		PickCPtr earliestxxl = 0;
		for (auto &a: origin->arrivals) {
			if ( ! a.pick->xxl)
				continue;

			xxlcount++;
			if (earliestxxl==0)
				earliestxxl = a.pick;
			else if (a.pick->time < earliestxxl->time)
				earliestxxl = a.pick;
		}

		// if there are enough XXL picks, only use these
		if (xxlcount >= _config.xxlMinPhaseCount) {

			OriginPtr copy = new Origin(*origin);
			// exclude those picks which are (in time) before the
			// earliest XXL pick
			size_t excludedCount = 0;
			size_t arrivalCount = origin->arrivals.size();
			for (size_t i=0; i<arrivalCount; i++) {
				Arrival &arr = origin->arrivals[i];
				if ( ! arr.pick->xxl &&
				     arr.pick->time < earliestxxl->time) {
					copy->arrivals[i].excluded =
						Arrival::ManuallyExcluded;
					excludedCount++;
				}
			}
			if (excludedCount) {
				if (_config.defaultDepthStickiness > 0.9) {
					_relocator.useFixedDepth(true);
				}
				else
					_relocator.useFixedDepth(false);

				copy->depthType = Origin::DepthFree;
				copy->lat = earliestxxl->station()->lat;
				copy->lon = earliestxxl->station()->lon;
				LOG_RELOCATOR_CALL;
				OriginPtr relo = _relocator.relocate(copy.get());
				if (relo) {
					origin->updateFrom(relo.get());
					SEISCOMP_INFO_S(
						" XXL "+printOneliner(origin));
					return true;
				}
			}
		}
	}

	// try to enhance score by excluding outliers
//	while (origin->definingPhaseCount() >= _config.minPhaseCount) {
	NEVER_RUN_THIS_LOOP { // XXX FIXME XXX TEMP XXX

		if (++loops > maxloops)
			break;

		double currentScore = _score(origin);
		double bestScore = currentScore;
		int    bestExcluded = -1;

		int arrivalCount = origin->arrivals.size();
		for (int i=0; i<arrivalCount; i++) {

			Arrival &a = origin->arrivals[i];
			if (a.excluded)
				continue;

			OriginPtr copy = new Origin(*origin);
			copy->arrivals[i].excluded = Arrival::ManuallyExcluded;

			_relocator.useFixedDepth(false);
			LOG_RELOCATOR_CALL;
			OriginPtr relo = _relocator.relocate(copy.get());
			if ( ! relo) {
				// try again, now using fixed depth (this sometimes helps)
				// TODO: figure out why this sometimes helps and whether there is a better way
				_relocator.useFixedDepth(true);
				LOG_RELOCATOR_CALL;
				relo = _relocator.relocate(copy.get());
				if ( ! relo)
					continue;
			}

			double score = _score(relo.get());

			if (score>bestScore) {
				bestScore = score;
				bestExcluded = i;
			}

			a.excluded = Arrival::NotExcluded;
		}

		if (bestExcluded == -1)
			break;

		// New experimental criterion to avoid endless exclusions
		// followed by inclusions of the same picks.
		// TODO: review, maybe there is a better criterion
		if (bestScore < currentScore+0.2)
			break;

		OriginPtr copy = new Origin(*origin);
		Arrival &a = copy->arrivals[bestExcluded];
		a.excluded = Arrival::LargeResidual;

		_relocator.useFixedDepth(false);
		LOG_RELOCATOR_CALL;
		OriginPtr relo = _relocator.relocate(copy.get());
		if ( ! relo) {
			// try again, now using fixed depth (this sometimes helps)
			_relocator.useFixedDepth(true);
			LOG_RELOCATOR_CALL;
			relo = _relocator.relocate(copy.get());
			if ( ! relo) // give up if fixing depth didn't help
				continue;
		}

		if (bestScore > 5)  // don't spoil the log
			SEISCOMP_DEBUG_S(
				" ENH " + printOneliner(relo.get()) +
				" exc " + a.pick->id());

		origin->updateFrom(relo.get());
		count ++;
	}

	return (count > 0);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::_rename_P_PKP(Autoloc::DataModel::Origin *origin)
{
	// Get phase naming right.
	// FIXME: Shouldn't be required any more.

	using namespace Autoloc::DataModel;

	for (auto &a: origin->arrivals) {
		double dt = a.pick->time-origin->time;

		if ( a.distance > 105 && dt > 960 && a.phase == "P" ) {
			a.phase = "PKP";
		}
		if ( a.distance < 125 && dt < 960 && isPKP(a.phase)) {
			a.phase = "P";
		}
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
double Autoloc3::_testFake(Autoloc::DataModel::Origin *origin) const
{
	using namespace Autoloc::DataModel;

	// Perform a series of tests to figure out of this origin is possibly
	// a fake origin resulting from wrong phase identification. It
	// measures how many of the picks may be misassociated.

	// TODO: Review!


	if ( imported(origin) )
		return 0.;

	if ( manual(origin) )
		return 0.;

	if ( origin->score > 80 ) {
		// can safely skip this test
		return 0.;
	}

	double maxProbability = 0;
	int arrivalCount = origin->arrivals.size();

	for (const auto otherOrigin : _origins) {
		int count = 0;

		// First very crude checks

		// We want to compare this origin with other *previous*
		// origins, so we restrict the time window accordingly
		if (otherOrigin->time < origin->time-1800 ||
		    otherOrigin->time > origin->time+ 600)
			continue;

		// We want to compare this origin with origins that
		// have significantly more picks, as otherwise the chance for
		// enough secondary phases is small anyway.
		// 
		// This is risky, because a new origin naturally has few picks initially
		if (otherOrigin->definingPhaseCount() < 2*origin->definingPhaseCount()) {
			continue;
		}

		// Now, for our origin, count the possible conincidences with
		// later phases of the other origin
		int definingPhaseCount = origin->definingPhaseCount();
		for (int i=0; i<arrivalCount; i++) {

			Arrival &arr = origin->arrivals[i];
//			if (arr.excluded)
//				continue;

			// see if otherOrigin references this pick already
			int iarr = otherOrigin->findArrival(arr.pick.get());
			if (iarr != -1) {
				const Arrival &oarr =
					otherOrigin->arrivals[iarr];
//				if ( ! arr.excluded) {
					arr.excluded = Arrival::DeterioratesSolution;
					SEISCOMP_DEBUG(
						"_testFake: doubly associated "
						"pick %s",
						oarr.pick->id().c_str());
					count ++;
					continue;
//				}
			}


			// now test for various phases
			const Station *sta = arr.pick->station();
			double delta, az, baz, depth=otherOrigin->dep;
			delazi(otherOrigin.get(), sta, delta, az, baz);
			Seiscomp::TravelTimeTable ttt;
			Seiscomp::TravelTimeList *ttlist =
				ttt.compute(otherOrigin->lat, otherOrigin->lon, otherOrigin->dep, sta->lat, sta->lon, 0);
			if (delta > 30) {
				const Seiscomp::TravelTime *tt = getPhase(ttlist, "PP");
				if (tt != nullptr && ! arr.pick->xxl && arr.score < 1) {
					double dt = arr.pick->time - (otherOrigin->time + tt->time);
					if (dt > -20 && dt < 30) {
						if (std::abs(dt) < std::abs(arr.residual))
							arr.excluded = Arrival::DeterioratesSolution;
						SEISCOMP_DEBUG("_testFake: %-6s %5lu %5lu PP   dt=%.1f", sta->code.c_str(),origin->id, otherOrigin->id, dt);
						count ++;
						delete ttlist;
						continue;
					}
				}
			}

			if (delta > 100) {
				const Seiscomp::TravelTime *tt = getPhase(ttlist, "PKP");
				if (tt != nullptr && ! arr.pick->xxl) {
					double dt = arr.pick->time - (otherOrigin->time + tt->time);
					if (dt > -20 && dt < 50) { // a bit more generous for PKP
						if (std::abs(dt) < std::abs(arr.residual))
							arr.excluded = Arrival::DeterioratesSolution;
						SEISCOMP_DEBUG("_testFake: %-6s %5lu %5lu PKP  dt=%.1f", sta->code.c_str(),origin->id, otherOrigin->id, dt);
						count ++;
						delete ttlist;
						continue;
					}
				}
			}

/* TODO: make SKP work properly
			if (delta > 120 && delta < 142) {
				const Seiscomp::TravelTime *tt = getPhase(ttlist, "SKP");
				if (tt != nullptr && ! arr.pick->xxl) {
					double dt = arr.pick->time - (otherOrigin->time + tt->time);
					if (dt > -20 && dt < 50) { // a bit more generous for SKP
						if (std::abs(dt) < std::abs(arr.residual))
							arr.excluded = Arrival::DeterioratesSolution;
						SEISCOMP_DEBUG("_testFake: %-6s %5lu %5lu SKP  dt=%.1f", sta->code.c_str(),origin->id, otherOrigin->id, dt);
						count ++;
						delete ttlist;
						continue;
					}
				}
			}
*/
			if (delta > 100 && delta < 130) { // preliminary! TODO: need to check amplitudes
				const Seiscomp::TravelTime *tt = getPhase(ttlist, "PKKP");
				if (tt != nullptr && ! arr.pick->xxl) {
					double dt = arr.pick->time - (otherOrigin->time + tt->time);
					if (dt > -20 && dt < 50) { // a bit more generous for PKKP
						if (std::abs(dt) < std::abs(arr.residual))
							arr.excluded = Arrival::DeterioratesSolution;
						SEISCOMP_DEBUG("_testFake: %-6s %5lu %5lu PKKP dt=%.1f", sta->code.c_str(),origin->id, otherOrigin->id, dt);
						count ++;
						delete ttlist;
						continue;
					}
				}
			}

			if (delta > 25 && depth > 60) {
				const Seiscomp::TravelTime *tt = getPhase(ttlist, "pP");
				if (tt) {
					double dt = arr.pick->time - (otherOrigin->time + tt->time);
					if (dt > -20 && dt < 30) {
						if (std::abs(dt) < std::abs(arr.residual))
							arr.excluded = Arrival::DeterioratesSolution;
						SEISCOMP_DEBUG("_testFake: %-6s %5lu %5lu pP   dt=%.1f", sta->code.c_str(),origin->id, otherOrigin->id, dt);
						count ++;
						delete ttlist;
						continue;
					}
				}

				tt = getPhase(ttlist, "sP");
				if (tt) {
					double dt = arr.pick->time - (otherOrigin->time + tt->time);
					if (dt > -20 && dt < 30) {
						if (std::abs(dt) < std::abs(arr.residual))
							arr.excluded = Arrival::DeterioratesSolution;
						SEISCOMP_DEBUG("_testFake: %-6s %5lu %5lu sP   dt=%.1f", sta->code.c_str(),origin->id, otherOrigin->id, dt);
						count ++;
						delete ttlist;
						continue;
					}
				}
			}

			if (delta < 110) {
				const Seiscomp::TravelTime *tt = getPhase(ttlist, "S"); // includes SKS!
				if (tt != nullptr && ! arr.pick->xxl && arr.score < 1) {
					double dt = arr.pick->time - (otherOrigin->time + tt->time);
					if (dt > -20 && dt < 30) {
						if (std::abs(dt) < std::abs(arr.residual))
							arr.excluded = Arrival::DeterioratesSolution;
						SEISCOMP_DEBUG("_testFake: %-6s %5lu %5lu S    dt=%.1f", sta->code.c_str(),origin->id, otherOrigin->id, dt);
						count ++;
						delete ttlist;
						continue;
					}
				}
			}

			// TODO: We might actually be able to skip the phase test here
			// if we can more generously associate phases to the "good" origin
			// (loose association). In that case we only need to test if
			// a pick is referenced by an origin with a (much) higher score.
			delete ttlist;
		}

		if (count) {
			SEISCOMP_DEBUG(
				"_testFake: %ld -> %ld, %d/%d",
				origin->id, otherOrigin->id,
				count, definingPhaseCount);
		}

		double probability = double(count)/definingPhaseCount;
//		if (count > maxCount)
//			maxCount = count;
		if (probability > maxProbability)
			maxProbability = probability;
	}

/*
	if (maxCount) {
//		double probability = double(maxCount)/origin->definingPhaseCount();
		double probability = double(maxCount)/arrivalCount;
		return probability;
	}
*/

	return maxProbability;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
int Autoloc3::_removeWorstOutliers(Autoloc::DataModel::Origin *origin)
{
	using namespace Autoloc::DataModel;

#ifdef EXTRA_DEBUGGING
	std::string info_before = printDetailed(origin);
	SEISCOMP_DEBUG("_removeWorstOutliers begin");
#endif
	int count = 0;
	std::vector<std::string> removed;

	for (ArrivalVector::iterator
	     it = origin->arrivals.begin(); it != origin->arrivals.end();) {

		Arrival &arr = *it;

		if (arr.excluded &&
		    std::abs(arr.residual) > _config.maxResidualKeep) {

			arr.pick->setOriginID(0); // disassociate the pick
			removed.push_back(arr.pick->id());
			it = origin->arrivals.erase(it);
			count++;
			// TODO try to re-associate the released pick
			//      with other origin
		}
		else ++it;
	}

	if (count==0)
		return 0;

#ifdef EXTRA_DEBUGGING
	std::string info_after = printDetailed(origin);
	// logging only
	int n = removed.size();
	std::string tmp=removed[0];
	for(int i=1; i<n; i++)
		tmp += ", "+removed[i];
	SEISCOMP_DEBUG("removed outlying arrivals from origin %ld: %s", origin->id, tmp.c_str());
	SEISCOMP_DEBUG_S("Before:\n" + info_before);
	SEISCOMP_DEBUG_S("After:\n"  + info_after);
	SEISCOMP_DEBUG("_removeWorstOutliers end");
#endif
	return count;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
static bool is_P_arrival(const Autoloc::DataModel::Arrival &a)
{
	return (a.phase=="P"  || a.phase=="Pn" ||
		a.phase=="Pg" || a.phase=="Pb");
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_residualWithinAllowedRange(
	const Autoloc::DataModel::Arrival &a,
	double minFactor, double maxFactor) const
{
	using namespace Autoloc::DataModel;

	// TODO: Review!
	// We adjust the residual range via minFactor, maxFactor.
	// This might result in behaviour that is difficult to
	// understand.
	double minResidual = -minFactor*_config.maxResidualUse;
	double maxResidual =  maxFactor*_config.maxResidualUse;

	double residual = a.residual;

	if ( _config.aggressivePKP && isPKP(a.phase) )
		residual *= 0.5;

	if ( is_P_arrival(a) ) {
		// Hack to tolerate larger residuals for regional phases.
		//
		// This regional weight shall accommodate moderate errors
		// in source depth that might otherwise prevent association
		// of a pick, but also Pg wrongly associated as P.
		//
		// TODO: Review
		double regionalWeight =
			1. + 0.7*exp(-a.distance*a.distance/50.);
		residual /= regionalWeight;
	}

	if (residual < minResidual || residual > maxResidual)
		return false;

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_trimResiduals(Autoloc::DataModel::Origin *origin)
{
	using namespace Autoloc::DataModel;

	int arrivalCount = origin->arrivals.size();
	int count = 0;

	while (origin->definingPhaseCount() >= _config.minPhaseCount) {
		double maxResidual = 0;
		int    maxIndex = -1;

		for (int i=0; i<arrivalCount; i++) {

			Arrival &arr = origin->arrivals[i];
			if (arr.excluded)
				continue;

			double normalizedResidual =
				arr.residual/_config.maxResidualUse;
			// add some penalty for positive residuals
			// TODO: make it configurable?
			if (normalizedResidual > 0)
				normalizedResidual *= 1.5;

			// If the residual is bad, keep track of the
			// worst residual
			if (std::abs(normalizedResidual) > maxResidual) {
				maxIndex = i;
				maxResidual = std::abs(normalizedResidual);
			}
		}

		if (maxIndex == -1)
			break;

		if (std::abs(maxResidual) < 1)
			break;

		OriginPtr copy = new Origin(*origin);
		Arrival &arr = copy->arrivals[maxIndex];
		arr.excluded = Arrival::LargeResidual;

		// NOTE that the behavior of _relocator is configured 
		// outside this routine
		LOG_RELOCATOR_CALL;
		OriginPtr relo = _relocator.relocate(copy.get());
		if ( ! relo)
			break;

		origin->updateFrom(relo.get());
		SEISCOMP_DEBUG_S(
			" TRM " + printOneliner(relo.get()) +
			" exc " + arr.pick->id());
		count ++;
	}

	// try to get some large-residual picks back into the solution
	while (true) {
		double minResidual = 1000;
		int    minIndex = -1;

		for (int i=0; i<arrivalCount; i++) {

			Arrival &arr = origin->arrivals[i];
			if (arr.excluded != Arrival::LargeResidual)
				continue;

			if (std::abs(arr.residual) < minResidual) {
				minIndex = i;
				minResidual = std::abs(arr.residual);
			}
		}

		// if the best excluded pick is either not found
		// or has too big a residual, stop here
		if (minIndex == -1 || minResidual > 2*_config.goodRMS)
			break;

		OriginPtr copy = new Origin(*origin);
		Arrival &arr = copy->arrivals[minIndex];
		arr.excluded = Arrival::NotExcluded;

		SEISCOMP_DEBUG_S(
			" TRX " + printOneliner(origin));
		SEISCOMP_DEBUG_S(
			" TRY " + printOneliner(copy.get()));
		LOG_RELOCATOR_CALL;
		OriginPtr relo = _relocator.relocate(copy.get());
		if ( ! relo)
			break;

		origin->updateFrom(relo.get());
		SEISCOMP_DEBUG_S(
			" TRM " + printOneliner(relo.get()) +
			" inc " + arr.pick->id());
		count ++;
	}

	return count > 0;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::setupStation(
	const Seiscomp::DataModel::WaveformStreamID &wfid,
	const Seiscomp::Core::Time &time)
{
	// search for matching station in inventory
	// FIXME: Location code is used in the key but
	// FIXME: the actual sensor location is not (yet) used.
	const std::string key =
		wfid.networkCode() + "." + wfid.stationCode() + "." + wfid.locationCode();
	for (size_t n=0; n < scinventory->networkCount(); ++n) {
		const Seiscomp::DataModel::Network
			*network = scinventory->network(n);
		if ( network->code() != wfid.networkCode() )
			continue;
		try {
			if ( time < network->start() )
				continue;
		}
		catch ( ... ) {
			// no network start time -> can be ignored
		}

		try {
			if ( time > network->end() )
				continue;
		}
		catch ( ... ) {
			// no network end time -> can be ignored
		}

		for (size_t s=0; s < network->stationCount(); ++s) {
			Seiscomp::DataModel::Station *station =
				network->station(s);

			if (station->code() != wfid.stationCode())
				continue;

			std::string
				epochStart = "unset",
				epochEnd = "unset";

			try {
				if (time < station->start())
					continue;
				epochStart =
					station->start().toString("%FT%TZ");
			}
			catch ( ... ) { }

			try {
				if (time > station->end())
					continue;
				epochEnd = station->end().toString("%FT%TZ");
			}
			catch ( ... ) { }

			SEISCOMP_DEBUG(
				"Station %s %s epoch %s ... %s",
				network->code().c_str(),
				station->code().c_str(),
				epochStart.c_str(),
				epochEnd.c_str());

			Autoloc::DataModel::Station *sta =
				new Autoloc::DataModel::Station(station);

			const StationConfigItem &c
				= _stationConfig.get(sta->net, sta->code);
			sta->maxNucDist = c.maxNucDist;
			sta->maxLocDist = 180;
			sta->enabled = c.usage > 0;

			_stations[key] = sta;

			// propagate to _nucleator and _relocator
			_relocator.setStation(sta);
			_nucleator.setStation(sta);

			SEISCOMP_DEBUG(
				"Initialized station %-8s", key.c_str());

			return true;
		}
	}

	SEISCOMP_WARNING("%s not found in station inventory", key.c_str());
	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::setupStation(
	const Seiscomp::DataModel::Pick *scpick)
{
	if ( ! scpick)
		return false;

	// search for station among the already configured stations
	const Seiscomp::DataModel::WaveformStreamID &wfid =
		scpick->waveformID();
	const Seiscomp::Core::Time &time = scpick->time().value();
	std::string key =
		wfid.networkCode() + "." + wfid.stationCode() + "." + wfid.locationCode();
	if (_stations.find(key) == _stations.end()) {
		// only set up station if is was not found
		if ( ! setupStation(wfid, time))
			return false;
	}
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::reconfigureStations()
{
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::setLocatorProfile(const std::string &profile) {
	SEISCOMP_DEBUG_S("Setting configured locator profile: " + profile);
	_nucleator.setLocatorProfile(profile);
	_relocator.setProfile(profile);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::setConfig(const Autoloc::Config &config) {
	_config = config;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::setConfig(const Seiscomp::Config::Config *conf) {
	scconfig = conf;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::setInventory(const Seiscomp::DataModel::Inventory *inv) {
	scinventory = inv;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::setPickLogFilePrefix(const std::string &fname)
{
	_pickLogFilePrefix = fname;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::setPickLogFileName(const std::string &fname)
{
	if (fname == _pickLogFileName && _pickLogFile.is_open() )
		return;

	// log file name has changed and/or file is not open

	if ( _pickLogFile.is_open() )
		_pickLogFile.close();

	_pickLogFileName = fname;
	if (fname == "")
		return;

	_pickLogFile.open(fname.c_str(), std::ios_base::app);
	if ( ! _pickLogFile.is_open() ) {
		SEISCOMP_ERROR_S("Failed to open pick log file " + fname);
		return;
	}
	SEISCOMP_INFO_S("Logging picks to file " + fname);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::setStationConfigFilename(const std::string &filename)
{
	_stationConfig.setFilename(filename);
	if (_stationConfig.read())
		return;
	SEISCOMP_ERROR_S("Failed to read station config file "+filename);
	exit(1);
		
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
const std::string &Autoloc3::stationConfigFilename() const
{
	return _stationConfig.filename();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::reset()
{
	SEISCOMP_INFO("reset requested");
	_associator.reset();
	_nucleator.reset();
	_outgoing.clear();
	_origins.clear();
	_lastSent.clear();
	pickPool.clear();
	_newOrigins.clear();
//	cleanup(now());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::shutdown()
{
	SEISCOMP_INFO("shutting down autoloc");

	reset();
	_associator.shutdown();
	_nucleator.shutdown();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::trackPick(const std::string &pickID)
{
	_trackedPicks.push_back(pickID);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::isTrackedPick(const std::string &pickID) const
{
	return std::find(_trackedPicks.begin(), _trackedPicks.end(), pickID)
		!= _trackedPicks.end();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<



// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::cleanup(Autoloc::DataModel::Time minTime)
{
	using namespace Autoloc::DataModel;

	if ( ! minTime) {
		minTime = _now - _config.maxAge - 1800;

		if (_now < _nextCleanup)
			return;
		if (_config.maxAge <= 0)
			return;
	}

	int beforePickCount   = Pick::count();
	int beforeOriginCount = Origin::count();

	// clean up pick pool
	for(PickPool::iterator
	    it = pickPool.begin(); it != pickPool.end(); ) {

		PickCPtr pick = it->second;
		if (pick->time < minTime)
			pickPool.erase(it++);
		else ++it;
	}

	int nclean = _nucleator.cleanup(minTime);
	SEISCOMP_INFO(
		"CLEANUP: Nucleator: %d items removed", nclean);
	_nextCleanup = _now + _config.cleanupInterval;
	SEISCOMP_INFO(
		"CLEANUP **** picks    %d / %d",
		beforePickCount, Pick::count());
	SEISCOMP_INFO(
		"CLEANUP **** origins  %d / %d",
		beforeOriginCount, Origin::count());

	OriginVector _originsTmp;
	for(OriginVector::iterator
	    it = _origins.begin(); it != _origins.end(); ++it) {

		OriginPtr origin = *it;

		if (origin->time < minTime)
			continue;

		_originsTmp.push_back(origin);
	}
	_origins = _originsTmp;


	std::vector<OriginID> ids;
	for (std::map<int, OriginPtr>::iterator
	     it = _lastSent.begin(); it != _lastSent.end(); ++it) {

		const Origin *origin = it->second.get();
		if (origin->time < minTime)
			ids.push_back(origin->id);
	}

	for(std::vector<OriginID>::iterator
	    it = ids.begin(); it != ids.end(); ++it) {

		OriginID id = *it;
		_lastSent.erase(id);
	}
	dumpState();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_depthIsResolvable(Autoloc::DataModel::Origin *origin)
{
	using namespace Autoloc::DataModel;

//	if (depthPhaseCount(origin)) {
//		origin->depthType = Origin::DepthPhases;
//		return true;
//	}

	if (origin->depthType == Origin::DepthDefault &&
	    origin->dep != _config.defaultDepth)
		origin->depthType = Origin::DepthFree;

	OriginPtr test = new Origin(*origin);
	_relocator.useFixedDepth(false);
	test->depthType = Origin::DepthFree;
	LOG_RELOCATOR_CALL;
	OriginPtr relo = _relocator.relocate(test.get());
	if (relo) {
#ifdef EXTRA_DEBUGGING
		SEISCOMP_DEBUG(
			"_depthIsResolvable for origin %ld: dep=%.1f "
			"smaj=%.1f sdep=%.1f stim=%.1f",
			origin->id, relo->dep, relo->error.semiMajorAxis,
			relo->error.sdepth, relo->error.stime);
#endif
		if (relo->error.sdepth > 0.) {
			if (relo->error.sdepth < 15*relo->error.stime) {
#ifdef EXTRA_DEBUGGING
				SEISCOMP_DEBUG(
					"_depthIsResolvable passed for "
					"origin %ld (using new criterion A)",
					origin->id);
#endif
				return true;
			}
			if (relo->error.sdepth < 0.7*relo->dep) {
#ifdef EXTRA_DEBUGGING
				SEISCOMP_DEBUG(
					"_depthIsResolvable passed for "
					"origin %ld (using new criterion B)",
					origin->id);
#endif
				return true;
			}
		}
	}
#ifdef EXTRA_DEBUGGING
	else {
		SEISCOMP_DEBUG(
			"_depthIsResolvable failed for origin %ld", origin->id);
	}
#endif

#ifdef EXTRA_DEBUGGING
	SEISCOMP_DEBUG(
		"_depthIsResolvable poorly for origin %ld "
		"(using new criterion)", origin->id);
	SEISCOMP_DEBUG("_depthIsResolvable using old criterion now");
#endif

	test = new Origin(*origin);
	test->dep = _config.defaultDepth;
	_relocator.useFixedDepth(true);
	LOG_RELOCATOR_CALL;
	relo = _relocator.relocate(test.get());
	if ( ! relo) {
		// if we fail to relocate using a fixed shallow depth, we
		// assume that the original depth is resolved.
#ifdef EXTRA_DEBUGGING
		SEISCOMP_DEBUG(
			"_depthIsResolvable passed for origin %ld "
			"(using old criterion A)", origin->id);
#endif
		return true;
	}

	// relo here has the default depth (fixed)
	double score1 = _score(origin), score2 = _score(relo.get());
	if ( score2 < 0.8*score1 ) {
#ifdef EXTRA_DEBUGGING
		SEISCOMP_DEBUG(
			"_depthIsResolvable passed for origin %ld "
			"(using old criterion B)", origin->id);
#endif
		return true;
	}

	if (origin->dep != relo->dep)
		SEISCOMP_INFO(
			"Origin %ld: changed depth from %.1f to default "
			"of %.1f   score: %.1f -> %.1f",
			origin->id, origin->dep, relo->dep, score1, score2);
	origin->updateFrom(relo.get());
	origin->depthType = Origin::DepthDefault;
	_updateScore(origin); // why here?

#ifdef EXTRA_DEBUGGING
	SEISCOMP_DEBUG("_depthIsResolvable poorly for origin %ld", origin->id);
#endif
	return false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::sync(const Seiscomp::Core::Time &sctime)
{
	Autoloc::DataModel::Time t(sctime);
	if (t > _now)
		_now = t;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::timeStamp() const
{
	SEISCOMP_DEBUG_S("Timestamp: "+sctime(_now).toString("%F %T.%f"));
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::dumpConfig() const {
	_config.dump();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


}  // namespace Autoloc

}  // namespace Seiscomp
