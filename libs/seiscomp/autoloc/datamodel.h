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

#ifndef SEISCOMP_LIBAUTOLOC_DATAMODEL_H_INCLUDED
#define SEISCOMP_LIBAUTOLOC_DATAMODEL_H_INCLUDED
#include <string>
#include <map>
#include <list>
#include <vector>

#include <seiscomp/core/baseobject.h>
#include <seiscomp/datamodel/station.h>
#include <seiscomp/datamodel/origin.h>
#include <seiscomp/datamodel/pick.h>
#include <seiscomp/datamodel/amplitude.h>

namespace Seiscomp {

namespace Autoloc {

namespace DataModel {

typedef size_t OriginID;
typedef double Time;


DEFINE_SMARTPOINTER(Station);

class Station : public Seiscomp::Core::BaseObject {

	public:
		Station(const Seiscomp::DataModel::Station*);

		std::string code, net;
		std::string loc; // to be used in the future
		double lat, lon, alt;

		// Max. nucleation distance
		double maxNucDist;

		// Max. location distance. Stations farther away from
		// epicenter may be associated but not used in location.
		double maxLocDist;

		// Is this station enabled at all?
		bool enabled;
};


typedef std::map<std::string, StationCPtr> StationMap;


DEFINE_SMARTPOINTER(Origin);
class Origin;


DEFINE_SMARTPOINTER(Pick);
class Pick;

class Pick : public Seiscomp::Core::BaseObject {

	public:
		typedef enum {
			Automatic,
			Manual,
			Confirmed,
			IgnoredAutomatic
		} Status;

	public:

		Pick(const Seiscomp::DataModel::Pick*);
		~Pick();

		static int count();

		// Attached SC objects. The pick must never be null.
		Seiscomp::DataModel::PickCPtr      scpick;
		Seiscomp::DataModel::AmplitudeCPtr scamplAbs;
		Seiscomp::DataModel::AmplitudeCPtr scamplSNR;

		const std::string& id() const {
			return scpick->publicID();
		}

		const std::string& net() const {
			return scpick->waveformID().networkCode();
		}

		const std::string& sta() const {
			return scpick->waveformID().stationCode();
		}

		const std::string& loc() const {
			return scpick->waveformID().locationCode();
		}

		const std::string& cha() const {
			return scpick->waveformID().channelCode();
		}

		const Station *station() const {
			return _station.get();
		}

		void setStation(const Station *sta) const;

		// sets the amplitude to either the SNR or absolute amplitude
		void setAmplitudeSNR(const Seiscomp::DataModel::Amplitude*);
		void setAmplitudeAbs(const Seiscomp::DataModel::Amplitude*);

		// get and set the origin this pick is associated with
		OriginID originID() const;
		void setOriginID(OriginID originID) const;

		Time time;	// pick time
		float amp;	// linear amplitude
		float per;	// period in seconds
		float snr;	// signal-to-noise ratio
		float normamp;	// normalized amplitude

		Status status;
		bool xxl;	// Does it look like a pick of a very big event?
		Time creationTime;

		mutable bool blacklisted;
		mutable int priority;

	private:
		// The (one and only) origin this pick is associated to
//		mutable OriginPtr _origin;
		mutable OriginID _originID;

		// Station information
		mutable StationPtr _station;
};


class Hypocenter : public Seiscomp::Core::BaseObject {

	public:

		Hypocenter(double lat, double lon, double dep)
			: lat(lat), lon(lon), dep(dep), laterr(0), lonerr(0), deperr(0) { }

		double lat, lon, dep;
		double laterr, lonerr, deperr;
};


class Arrival {

	public:
		Arrival();
		Arrival(const Pick *pick, const std::string &phase="P", double residual=0);
		Arrival(const Origin *origin, const Pick *pick, const std::string &phase="P", double residual=0, double affinity=1);
		Arrival(const Arrival &);

		OriginCPtr origin;
		PickCPtr pick;
		std::string phase;
		float residual;
//		float weight;
		float distance;
		float azimuth;
		float affinity;
		float score;

		// XXX temporary:
		float dscore, ascore, tscore;

		// Reasons for why a pick may be excluded from the computation of an
		// origin. This allows exclusion of suspicious picks independent of
		// their weight, which remains unmodified.
		enum ExcludeReason {
			NotExcluded      = 0,

			// if a maximum residual is exceeded
			LargeResidual    = 1,

			// e.g. PKP if many stations in 0-100 deg distance range
			StationDistance  = 2,

			// Such a pick must remain excluded from the computation
			ManuallyExcluded = 4,

			// A pick that deteriorates the solution,
			// e.g. reduces score or badly increases RMS
			DeterioratesSolution = 8,

			// PP, SKP, SKS etc.
			UnusedPhase = 16,

			// pick temporarily excluded from the computation
			// XXX experimental XXX
			TemporarilyExcluded = 32,

			// pick has been blacklisted
			BlacklistedPick = 64
		};

		ExcludeReason excluded;
};


class ArrivalVector : public std::vector<Arrival> {
	public:
		bool sort();
};


class OriginQuality
{
	public:
		OriginQuality() : aziGapPrimary(360), aziGapSecondary(360) {}

		double aziGapPrimary;
		double aziGapSecondary;
};



class OriginError
{
	public:
		OriginError() : sdepth(0), stime(0), sdobs(0), conf(0) {}

	double sdepth, stime, sdobs, conf;
};



class Origin : public Hypocenter {

	public:
		enum ProcessingStatus {
			New,
			Updated,
			Deleted
		};

		enum LocationStatus {
			// purely automatic
			Automatic,

			// automatic, but confirmed
			ConfirmedAutomatic,

			// manual with automatic additions
			SemiManual,

			// purely manual
			Manual  
		};

		enum DepthType {
			// depth is sufficiently well constrained by the data
			DepthFree,

			// depth is constrained by depth phases
			DepthPhases,

			// locator wants to put origin at zero depth
			//   -> use reasonable default
			DepthMinimum,

			// no depth resolution -> use reasonable default
			DepthDefault,

			// must not be overridden by autoloc
			DepthManuallyFixed
		};

	public:

		Origin(double lat, double lon, double dep, const Time &time);
		Origin(const Origin&);
		~Origin();

		static int count();

		void updateFrom(const Origin*);

		// Add an arrival to the origin. This checks if the pick
		// references by the arrival is already associated to the
		// origin and if so, the arrival is not added and false is
		// returned. Upon success, true is returned.
		bool add(const Arrival &arr);

		// Adopt a pick. This means that the origin assumes
		// "ownership" of this pick, which prevents it to be "owned"
		// by another origin.
		bool adopt(const Pick *pick) const;

		// Return index of arrival referencing the exact same pick
		// or -1 if not found
		int findArrival(const Pick *pick) const;

		// Count the defining phases, optionally within a distance range
		int phaseCount(double dmin=0., double dmax=180.) const;
		int definingPhaseCount(double dmin=0., double dmax=180.) const;

		// count the association stations
		int definingStationCount() const;
		int associatedStationCount() const;

		// Compute the pick RMS for all picks used in the solution.
		double rms() const;

		// Compute the median station distance.
		//
		// Returns a negative number if the number of used
		// stations is zero. Perhaps replace this by an
		// exception.
		double medianStationDistance() const;

		void geoProperties(double &min, double &max, double &gap) const;

	public:
		OriginID id;

		bool imported;
		bool manual;
		bool preliminary;

		// A locked origin cannot be relocated.
		bool locked; 

		std::string publicID;

		std::string methodID;
		std::string earthModelID;

		ProcessingStatus processingStatus;
		LocationStatus locationStatus;

		double score;

		// lat,lon,dep are inherited from Hypocenter
		Time time, timestamp;
		double timeerr;
		DepthType depthType;
		ArrivalVector arrivals;
		OriginQuality quality;
		OriginError error;

	public:
		// This is the reference origin, if there is any.
		// A reference origin is a trusted origin that this origin is
		// derived from to some degree.
		OriginPtr referenceOrigin;
};


class OriginVector : public std::vector<OriginPtr> {

	public:
		// Return true if the origin instance is in the vector.
		bool find(const Origin *) const;

		// Return origin with the origin ID if found, NULL otherwise
		Origin *find(const OriginID &id);
		const Origin *find(const OriginID &id) const;

		// Try to find the best Origin which possibly belongs to the
		// same event
		const Origin *bestEquivalentOrigin(const Origin *start) const;

		// Try to find Origins which possibly belong to the same
		// event and try to merge the picks
		int mergeEquivalentOrigins(const Origin *start=0);
};


DEFINE_SMARTPOINTER(Event);

/*
class Event : public Seiscomp::Core::BaseObject {

	public:

		OriginVector origin;
};
*/


typedef std::map<std::string, Autoloc::DataModel::PickCPtr> PickPool;
typedef std::vector<PickPtr> PickVector;
typedef std::vector<Pick*>   PickGroup;



bool automatic(const Pick*);
bool ignored(const Pick*);
bool manual(const Pick*);
char statusFlag(const Pick*);
bool hasAmplitude(const Pick*);

}  // namespace DataModel

}  // namespace Autoloc

}  // namespace Seiscomp

#endif
