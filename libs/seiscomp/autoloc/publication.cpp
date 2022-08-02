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

#include <seiscomp/autoloc/autoloc.h>
#include <seiscomp/autoloc/util.h>
#include <seiscomp/autoloc/sc3adapters.h>
#include <seiscomp/logging/log.h>


namespace Seiscomp {

namespace Autoloc {


// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_report(Seiscomp::DataModel::Origin *scorigin)
{
	// This is a dummy intended to be overloaded properly.
	SEISCOMP_WARNING("Autoloc3::_report should be reimplemented");
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_report(const Autoloc::DataModel::Origin *origin)
{
	Seiscomp::DataModel::OriginPtr scorigin =
		Autoloc::exportToSC(origin, _config.reportAllPhases);

	Seiscomp::DataModel::CreationInfo ci;
	ci.setAgencyID(_config.agencyID);
	ci.setAuthor(_config.author);
	ci.setCreationTime(now());
	scorigin->setCreationInfo(ci);

	SEISCOMP_DEBUG ("Reporting origin:");
	SEISCOMP_DEBUG_S(printDetailed(origin));

	return _report(scorigin.get());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void Autoloc3::report()
{
	using namespace Autoloc::DataModel;

	for (OriginVector::iterator
	     it = _newOrigins.begin(); it != _newOrigins.end(); ) {

		Origin *origin = it->get();

		if (_nextDue.find(origin->id) == _nextDue.end())
			// first origin -> report immediately
			_nextDue[origin->id] = 0;

		_outgoing[origin->id] = origin;
		it = _newOrigins.erase(it);
	}


	Time t = now();
	std::vector<OriginID> ids;

	int dnmax = _config.publicationIntervalPickCount;

	for (std::map<int, OriginPtr>::iterator
	     it = _outgoing.begin(); it != _outgoing.end(); ++it) {

		const Origin *origin = it->second.get();
		double dt = t - _nextDue[origin->id];
		int dn = dnmax;

		if (_lastSent.find(origin->id) != _lastSent.end()) {
			size_t phaseCount =  origin->phaseCount();
			size_t lastPhaseCount = _lastSent[origin->id]->phaseCount();
			// size_t phaseCount =  origin->definingPhaseCount();
			// size_t lastPhaseCount = _lastSent[origin->id]->definingPhaseCount();
			dn = phaseCount - lastPhaseCount;
		}

		if (dt >= 0 || dn >= dnmax)
			ids.push_back(origin->id);
	}

	for (const OriginID &id: ids) {
		const Origin *origin = _outgoing[id].get();

		if ( ! _publishable(origin) ) {
			_outgoing.erase(id);
			continue;
		}

		// Test if we have previously sent an earlier version
		// of this origin. If so, test if the current version
		// has improved.
		// TODO: perhaps move this test to _publishable()
		if (_lastSent.find(id) != _lastSent.end()) {
			const Origin *previous = _lastSent[id].get();

			// The main criterion is definingPhaseCount.
			// However, there may be origins with additional
			// but excluded phases  like PKP and such
			// origins should also be sent.
			if (origin->definingPhaseCount() <=
			    previous->definingPhaseCount()) {

				if (origin->arrivals.size() <=
				    previous->arrivals.size() ||
				    _now - previous->timestamp < 150) {
					// TODO: make 150 configurable

					// ... some more robust criteria perhaps
					SEISCOMP_INFO(
						"Origin %ld not sent "
						"(no improvement)",
						origin->id);
					_outgoing.erase(id);
					continue;
				}
			}
		}

		if (_report(origin)) {
			SEISCOMP_INFO_S(" OUT " + printOneliner(origin));

			// Compute the time at which the next origin in this
			// series would be due to be reported, if any.
			int N = origin->definingPhaseCount();
			// This defines the minimum time interval between
			// adjacent origins to be reported. Larger origins may
			// put a higher burden on the system, but change less,
			// so larger time intervals are justified. The time
			// interval is a linear function of the defining phase
			// count.
			double
				A  = _config.publicationIntervalTimeSlope,
				B  = _config.publicationIntervalTimeIntercept,
				dt = A*N + B;

			if (dt < 0) {
				_nextDue[id] = 0;
				SEISCOMP_INFO(
					"Autoloc3::_flush() origin=%ld  "
					"next due IMMEDIATELY", id);
			}
			else {
				_nextDue[id] = t + dt;
				SEISCOMP_INFO(
					"Autoloc3::_flush() origin=%ld  "
					"next due: %s", id,
					time2str(_nextDue[id]).c_str());
			}

			// save a copy of the origin
			_lastSent[id] = new Origin(*origin);
			_lastSent[id]->timestamp = t;
			_outgoing.erase(id);
		}
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_passedFinalCheck(const Autoloc::DataModel::Origin *origin)
{
	using namespace Autoloc::DataModel;

// Do not execute the check here. It may result in missing origins which are
// correct after relocation, move the check to: Autoloc3::_publishable
//	if (origin->dep > _config.maxDepth) {
//		SEISCOMP_DEBUG("Ignore origin %ld: depth %.3f km > maxDepth %.3f km",
//		               origin->id, origin->dep, _config.maxDepth);
//		return false;
//	}

	if ( ! origin->preliminary &&
	     origin->definingPhaseCount() < _config.minPhaseCount)
		return false;

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool Autoloc3::_publishable(const Autoloc::DataModel::Origin *origin) const
{
	using namespace Autoloc::DataModel;

	if (origin->quality.aziGapSecondary > _config.maxAziGapSecondary) {
		SEISCOMP_INFO(
			"Origin %ld not sent (too large SGAP of %3.0f > %3.0f)",
			origin->id, origin->quality.aziGapSecondary,
			_config.maxAziGapSecondary);
		return false;
	}

	if (origin->score < _config.minScore) {
		SEISCOMP_INFO(
			"Origin %ld not sent (too low score of %.1f < %.1f)",
			origin->id, origin->score, _config.minScore);
		return false;
	}

	if (origin->rms() > _config.maxRMS) {
		SEISCOMP_INFO(
			"Origin %ld not sent (too large RMS of %.1f > %.1f)",
			origin->id, origin->rms(), _config.maxRMS);
		return false;
	}


	if (origin->dep > _config.maxDepth) {
		SEISCOMP_INFO(
			"Origin %ld too deep: %.1f km > %.1f km (maxDepth)",
			origin->id, origin->dep, _config.maxDepth);
		return false;
	}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


}  // namespace Autoloc

}  // namespace Seiscomp
