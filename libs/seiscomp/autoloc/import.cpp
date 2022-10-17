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

#include <seiscomp/logging/log.h>
#include <seiscomp/datamodel/utils.h>
#include <seiscomp/datamodel/network.h>
#include <seiscomp/datamodel/station.h>


namespace Seiscomp {

namespace Autoloc {

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Autoloc::DataModel::Origin*
Autoloc3::importFromSC(
	const Seiscomp::DataModel::Origin *scorigin)
{
	double lat  = scorigin->latitude().value();
	double lon  = scorigin->longitude().value();
	double dep  = scorigin->depth().value();
	double time = double(scorigin->time().value() - Core::Time());

	Autoloc::DataModel::Origin *origin =
		new Autoloc::DataModel::Origin(lat, lon, dep, time);

	try {
		origin->laterr =
			0.5*scorigin->latitude().lowerUncertainty() +
			0.5*scorigin->latitude().upperUncertainty();
	}
	catch ( ... ) {
		try {
			origin->laterr  = scorigin->latitude().uncertainty();
		}
		catch ( ... ) {
			origin->laterr = 0;
		}
	}

	try {
		origin->lonerr =
			0.5*scorigin->longitude().lowerUncertainty() +
			0.5*scorigin->longitude().upperUncertainty();
	}
	catch ( ... ) {
		try {
			origin->lonerr = scorigin->longitude().uncertainty();
		}
		catch ( ... ) {
			origin->lonerr = 0;
		}
	}

	try {
		origin->deperr =
			0.5*scorigin->depth().lowerUncertainty() +
			0.5*scorigin->depth().upperUncertainty();
	}
	catch ( ... ) {
		try {
			origin->deperr = scorigin->depth().uncertainty();
		}
		catch ( ... ) {
			origin->deperr = 0;
		}
	}

	try {
		origin->timeerr =
			0.5*scorigin->time().lowerUncertainty() +
			0.5*scorigin->time().upperUncertainty();
	}
	catch ( ... ) {
		try {
			origin->timeerr = scorigin->time().uncertainty();
		}
		catch ( ... ) {
			origin->timeerr = 0;
		}
	}

	int arrivalCount = scorigin->arrivalCount();
	for (int i=0; i<arrivalCount; i++) {

		const std::string &pickID = scorigin->arrival(i)->pickID();
/*
		Seiscomp::DataModel::Pick *scpick = Seiscomp::DataModel::Pick::Find(pickID);
		if ( ! scpick) {
			SEISCOMP_ERROR_S("Pick " + pickID + " not found - cannot import origin");
			delete origin;
			return NULL;
			// TODO:
			// Trotzdem mal schauen, ob wir den Pick nicht
			// als Autoloc-Pick schon haben
		}
*/
		// Retrieve pick from our internal pick pool. This must not fail.
		const Autoloc::DataModel::Pick *pick = pickFromPool(pickID);

		// If the pick pool failed, we try the database
		// TODO: Use Cache here!
		if ( ! pick ) {
SEISCOMP_ERROR("CODE TEMPORARILY COMMENTED OUT");
/*
			Seiscomp::DataModel::PickPtr scpick =
				loadPick(pickID);
			Seiscomp::DataModel::AmplitudePtr scamplAbs =
				loadAmplitude(pickID, _config.amplTypeAbs);
			Seiscomp::DataModel::AmplitudePtr scamplSNR =
				loadAmplitude(pickID, _config.amplTypeSNR);

			if ( ! setupStation(scpick.get()))
				continue;

			Autoloc::DataModel::Pick *p = // FIXME
				new Autoloc::DataModel::Pick(scpick.get());
			_addStationInfo(p);

			if (scamplAbs)
				p->setAmplitudeAbs(scamplAbs.get());
			else
				SEISCOMP_WARNING_S("Missing amplitude "+pickID+" abs");

			if (scamplSNR)
				p->setAmplitudeSNR(scamplSNR.get());
			else
				SEISCOMP_WARNING_S("Missing amplitude "+pickID+" snr");

			storeInPool(p);

			if (scpick) {
				pick = pickFromPool(pickID);
				if (pick)
					SEISCOMP_INFO_S("Pick " + pickID + " loaded from database");
			}
*/
		}

		if ( ! pick ) {
			// FIXME: This may also happen after Autoloc cleaned up
			//        older picks, so the pick isn't available any more!
			// TODO: Use Cache here!
			SEISCOMP_ERROR_S(
				"Pick " + pickID + " not found in "
				"internal pick pool - SKIPPING this pick");
			if (Seiscomp::DataModel::PublicObject::Find(pickID)) {
				SEISCOMP_ERROR(
					"HOWEVER, this pick is present in "
					"pool of public objects. "
					"Are you doing an XML playback?");
			}

			// This actually IS an error but we try to work around
			// it instead of giving up on this origin completely.
			continue; // FIXME
//			delete origin;
//			return NULL;
		}

		Autoloc::DataModel::Arrival arr(
			pick
			//, const std::string &phase="P", double residual=0
			);
		try {
			arr.residual = scorigin->arrival(i)->timeResidual();
		}
		catch(...) {
			arr.residual = 0;
			SEISCOMP_WARNING("got arrival with timeResidual not set");
		}

		try {
			arr.distance = scorigin->arrival(i)->distance();
		}
		catch(...) {
			arr.distance = 0;
			SEISCOMP_WARNING("got arrival with distance not set");
		}

		try {
			arr.azimuth = scorigin->arrival(i)->azimuth();
		}
		catch(...) {
			arr.azimuth = 0;
			SEISCOMP_WARNING("got arrival with azimuth not set");
		}

		if (manual(scorigin)) {
			// for manual origins we allow secondary phases like pP
			arr.phase = scorigin->arrival(i)->phase();

			try {
				if (scorigin->arrival(i)->timeUsed() == false)
					arr.excluded = Autoloc::DataModel::Arrival::ManuallyExcluded;
			}
			catch(...) {
				// In a manual origin in which the time is not
				// explicitly used we treat the arrival as if
				// it was explicitly excluded.
				arr.excluded = Autoloc::DataModel::Arrival::ManuallyExcluded;
			}
		}

		origin->arrivals.push_back(arr);
	}

	origin->publicID = scorigin->publicID();

	try {
		// FIXME: In scolv the Origin::depthType may not have been set!
		Seiscomp::DataModel::OriginDepthType dtype = scorigin->depthType();
		if ( dtype == Seiscomp::DataModel::OriginDepthType(
				Seiscomp::DataModel::FROM_LOCATION) )
			origin->depthType = Autoloc::DataModel::Origin::DepthFree;

		else if ( dtype == Seiscomp::DataModel::OriginDepthType(
				Seiscomp::DataModel::OPERATOR_ASSIGNED) )
			origin->depthType = Autoloc::DataModel::Origin::DepthManuallyFixed;
	}
	catch(...) {
		SEISCOMP_WARNING("Origin::depthType is not set!");
		bool adoptManualDepth = _config.adoptManualDepth;
		if (manual(scorigin) && adoptManualDepth == true) {
			// This is a hack! We cannot know wether the operator
			// assigned a depth manually, but we can assume the
			// depth to be operator approved and this is better
			// than nothing.
			// TODO: Make this behavior configurable?
			origin->depthType = Autoloc::DataModel::Origin::DepthManuallyFixed;
			SEISCOMP_WARNING(
				"Treating depth as if it was manually fixed");
		}
		else {
			origin->depthType = Autoloc::DataModel::Origin::DepthFree;
			SEISCOMP_WARNING("Leaving depth free");
		}
	}

	// mark and log imported/manual origin
	origin->imported = ( objectAgencyID(scorigin) != _config.agencyID );
	origin->manual   =   manual(scorigin);

	return origin;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


}  // namespace Autoloc

}  // namespace Seiscomp
